#include "renderer.h"

#include <cassert>

#include <algorithm>
#include <format>
#include <forward_list>

#include "constants.h"
#include "enum-utils.h"
#include "feed.h"
#include "glew-helpers.h"
#include "list-helpers.h"
#include "util.h"
#include "vec.h"

namespace Render
{
    namespace
    {
        constexpr const char* builtin_vert_shader_path(VertShader shader)
        {
            switch (shader)
            {
            case VertShader::CameraTransform:
                return "../shaders/transform.vert";
            case VertShader::NoTransform:
                return "../shaders/no-transform.vert";
            case VertShader::OneOneTransform:
                return "../shaders/1-1-scale-transform.vert";
            }
            return "";
        }

        constexpr const char* builtin_frag_shader_path(FragShader shader)
        {
            switch (shader)
            {
            case FragShader::BasicColor:
                return "../shaders/basic_color.frag";
            case FragShader::SolidCircle:
                return "../shaders/solid-circle.frag";
            case FragShader::Image:
                return "../shaders/image.frag";
            case FragShader::Icon:
                return "../shaders/icon.frag";
            case FragShader::Text:
                return "../shaders/text.frag";
            case FragShader::BasicHSV:
                return "../shaders/basic-hsv.frag";
            case FragShader::BasicFade:
                return "../shaders/basic-fade.frag";
            case FragShader::BasicTextureBlend:
                return "../shaders/basic-texture-blend.frag";
            case FragShader::CRTWarp:
                return "../shaders/crt-warp.frag";
            case FragShader::CRTEasymode:
                return "../shaders/crt-easymode.frag";
            case FragShader::CRTGamemode:
                return "../shaders/crt-gamemode.frag";
            case FragShader::CRTEasymodeLinearize:
                return "../shaders/crt-easymode-linearize.frag";
            case FragShader::CRTEasymodeBlurHoriz:
                return "../shaders/crt-easymode-blur-horiz.frag";
            case FragShader::CRTEasymodeBlurVert:
                return "../shaders/crt-easymode-blur-vert.frag";
            case FragShader::CRTEasymodeThresh:
                return "../shaders/crt-easymode-threshold.frag";
            case FragShader::CRTEasymodeHalation:
                return "../shaders/crt-easymode-halation.frag";
            }
            return "";
        }

        enum class ShaderUniformLocation
        {
            Time,
            Resolution,
            CameraCoordFactor,
            CameraPos,
            CameraScale,
            PreviousPassTexture,
            CustomFloatValue1,
            CustomFloatValue2,
            CustomVec2Value1,
            CustomVec2Value2,
            CustomVec2Value3,
            Count
        };

        struct ShaderUniformInput
        {
            ShaderUniformLocation locus;
            const char* name;
        };

        constexpr ShaderUniformInput uniforms[count_of<ShaderUniformLocation>] {
            { .locus = ShaderUniformLocation::Time,
            .name = "time" },

            { .locus = ShaderUniformLocation::Resolution,
            .name = "resolution" },

            { .locus = ShaderUniformLocation::CameraCoordFactor,
            .name = "camera_coord_factor" },

            { .locus = ShaderUniformLocation::CameraPos,
            .name = "camera_pos" },

            { .locus = ShaderUniformLocation::CameraScale,
            .name = "camera_scale" },

            { .locus = ShaderUniformLocation::PreviousPassTexture,
            .name = "prev_pass_tex" },

            { .locus = ShaderUniformLocation::CustomFloatValue1,
            .name = "custom_float_value1" },

            { .locus = ShaderUniformLocation::CustomFloatValue2,
            .name = "custom_float_value2" },

            { .locus = ShaderUniformLocation::CustomVec2Value1,
            .name = "custom_vec2_value1" },

            { .locus = ShaderUniformLocation::CustomVec2Value2,
            .name = "custom_vec2_value2" },

            { .locus = ShaderUniformLocation::CustomVec2Value3,
            .name = "custom_vec2_value3" },
        };

        static_assert(std::is_sorted(std::begin(uniforms),
                                    std::end(uniforms),
                                    [](const auto& lhs, const auto& rhs)
                                    {
                                        return rep(lhs.locus) < rep(rhs.locus);
                                    }));

        template <typename T, int BindPosition>
        struct VertexBinding
        {
            T data;
            static constexpr int locus = BindPosition;
        };

        enum class VertexBindingLocus
        {
            Position,
            Color,
            UV,
            Count
        };

        // We model a vertex as a tuple of (pos, color, uv transformation).
        struct RenderVertex
        {
            VertexBinding<Vec2f, rep(VertexBindingLocus::Position)> pos;
            VertexBinding<Vec4f, rep(VertexBindingLocus::Color)> color;
            VertexBinding<Vec2f, rep(VertexBindingLocus::UV)> uv;
        };

        constexpr int vertex_cap = 3 * 25000;

        static_assert(vertex_cap % 3 == 0,
            "retain relation that the vertex cap is divisible by 3 since we're rendering triangles.");

        constexpr auto default_reporter = [](const std::string& s)
        {
            fprintf(stderr, "%s\n", s.c_str());
        };

        template <typename Reporter>
        Glew::ShaderHandle compile_shader_file(const char* path, Glew::ShaderType type, Reporter&& reporter)
        {
            std::string contents;
            auto err = read_file(path, &contents);
            if (err != Errno::OK)
            {
                char msg[512];
                strerror_s(msg, rep(err));
                auto txt = std::format("Failed to load '{}' shader file: {}", path, msg);
                reporter(txt);
                return { };
            }
            auto handle = Glew::compile_shader(type, contents.c_str(), reporter);
            if (not handle)
            {
                auto txt = std::format("Failed to compile shader file: {}", path);
                reporter(txt);
            }
            return handle;
        }

        using UniformsContainer = Glew::UniformHandle[count_of<ShaderUniformLocation>];
        void populate_uniform_locations(Glew::ProgramHandle program, UniformsContainer* container)
        {
            for (int i = 0; i < count_of<ShaderUniformLocation>; ++i)
            {
                (*container)[i] = Glew::UniformHandle{ glGetUniformLocation(rep(program), uniforms[i].name) };
            }
        }

        using ShaderProgramContainer = Glew::ScopedProgramHandle[count_of<VertShader>][count_of<FragShader>];
        enum class ColorAttachments : GLint
        {
            Default,
            RGBA = Default,

            Count,
        };

        constexpr GLuint color_attachment_index(ColorAttachments attachment)
        {
            return GL_COLOR_ATTACHMENT0 + rep(attachment);
        }

        enum class FrameBufferID : GLuint { };

        enum class InternalTextureFormat : GLuint
        {
            RGBA8 = GL_RGBA8,
            Uint32 = GL_R32UI,
        };

        constexpr InternalTextureFormat internal_texture_format_for_attachment(ColorAttachments attachment)
        {
            switch (attachment)
            {
            case ColorAttachments::RGBA:
                return InternalTextureFormat::RGBA8;
            default:
                assert(not "format not implemented");
                return InternalTextureFormat{ };
            }
        }

        enum class TextureFormat : GLuint
        {
            RGBA = GL_RGBA,
            RedInt = GL_RED_INTEGER,
        };

        constexpr TextureFormat texture_format_for_attachment(ColorAttachments attachment)
        {
            switch (attachment)
            {
            case ColorAttachments::RGBA:
                return TextureFormat::RGBA;
            default:
                assert(not "format not implemented");
                return TextureFormat{ };
            }
        }

        void attach_color_texture(GLuint tex_id, const ScreenDimensions& screen, ColorAttachments attachment)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, rep(internal_texture_format_for_attachment(attachment)), rep(screen.width), rep(screen.height), 0, rep(texture_format_for_attachment(attachment)), GL_UNSIGNED_BYTE, nullptr);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glFramebufferTexture2D(GL_FRAMEBUFFER, color_attachment_index(attachment), GL_TEXTURE_2D, tex_id, 0);
        }

        enum class DepthTextureFormat : GLuint
        {
            Depth24Stencil8 = GL_DEPTH24_STENCIL8
        };

        enum class DepthAttachment : GLuint
        {
            DepthStencilAttachment = GL_DEPTH_STENCIL_ATTACHMENT
        };

        void attach_depth_texture(GLuint tex_id, DepthTextureFormat format, const ScreenDimensions& screen, DepthAttachment attachment)
        {
            glTexStorage2D(GL_TEXTURE_2D, 1, rep(format), rep(screen.width), rep(screen.height));

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glFramebufferTexture2D(GL_FRAMEBUFFER, rep(attachment), GL_TEXTURE_2D, tex_id, 0);
        }

        template <int N>
        void create_textures(GLuint (&output)[N])
        {
            glCreateTextures(GL_TEXTURE_2D, N, output);
        }

        GLuint create_texture()
        {
            GLuint id;
            glCreateTextures(GL_TEXTURE_2D, 1, &id);
            return id;
        }

        template <int N>
        void delete_textures(GLuint (&output)[N])
        {
            glDeleteTextures(N, output);
        }

        void delete_texture(GLuint id)
        {
            glDeleteTextures(1, &id);
        }

        void bind_texture(GLuint id)
        {
            glBindTexture(GL_TEXTURE_2D, id);
        }

        struct FramebufferData
        {
            FrameBufferID id;
            GLuint attachments[rep(ColorAttachments::Count)];
            GLuint depth_attachment;
        };

        struct RenderTextureData
        {
            FramebufferData data;
            ScreenDimensions size;
        };

        using RenderTextureAlloc = std::forward_list<RenderTextureData>;

        // Global data shared across all renderer instances.
        GLuint vao;
        GLuint vbo;
        ShaderProgramContainer shader_programs;
        constinit RenderVertex vertices[vertex_cap]{};
        GLsizei vertices_flush_count = 0;
        FramebufferData framebuffer_collection[rep(Framebuffer::Count)];
        RenderTextureAlloc render_texture_allocator;
#ifndef NDEBUG
        // This is used to track the implicit dependency by using a single vertex buffer
        // above.  Two different renderers cannot invoke a render function without first
        // calling flush.
        SceneRenderer* current_renderer;
#endif

        void init_vertex_buffer()
        {
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);

            // Create the vertex buffer data binding.
            glGenBuffers(1, &vbo);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            // Note: the use of 'sizeof(array)' is intentional because we're providing a total buffer size.
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

            // position
            glEnableVertexAttribArray(rep(VertexBindingLocus::Position));
            glVertexAttribPointer(
                rep(VertexBindingLocus::Position),
                2, // Vec2f
                GL_FLOAT,
                GL_FALSE,
                sizeof(RenderVertex),
                (GLvoid *) __builtin_offsetof(RenderVertex, pos));

            // color
            glEnableVertexAttribArray(rep(VertexBindingLocus::Color));
            glVertexAttribPointer(
                rep(VertexBindingLocus::Color),
                4, // Vec4f
                GL_FLOAT,
                GL_FALSE,
                sizeof(RenderVertex),
                (GLvoid *) __builtin_offsetof(RenderVertex, color));

            // uv
            glEnableVertexAttribArray(rep(VertexBindingLocus::UV));
            glVertexAttribPointer(
                rep(VertexBindingLocus::UV),
                2, // Vec2f
                GL_FLOAT,
                GL_FALSE,
                sizeof(RenderVertex),
                (GLvoid *) __builtin_offsetof(RenderVertex, uv));
        }

        void setup_framebuffer_texture_attachments(FramebufferData* data, const ScreenDimensions& screen)
        {
            create_textures(data->attachments);
            for (auto i = ColorAttachments{}; i != ColorAttachments::Count; i = extend(i))
            {
                bind_texture(data->attachments[rep(i)]);
                attach_color_texture(data->attachments[rep(i)], screen, i);
            }

            // Attach the single depth texture.
            data->depth_attachment = create_texture();
            bind_texture(data->depth_attachment);
            attach_depth_texture(data->depth_attachment, DepthTextureFormat::Depth24Stencil8, screen, DepthAttachment::DepthStencilAttachment);

            GLenum buffers[rep(ColorAttachments::Count)] = { GL_COLOR_ATTACHMENT0 };
            glDrawBuffers(GLsizei(std::size(buffers)), buffers);

            assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        }

        void init_framebuffer(FramebufferData* data, const ScreenDimensions& screen)
        {
            // Right into the DANGER ZONE!!!
            glCreateFramebuffers(1, reinterpret_cast<GLuint*>(&data->id));
            glBindFramebuffer(GL_FRAMEBUFFER, rep(data->id));
            setup_framebuffer_texture_attachments(data, screen);
            // Bind to the default frame buffer on exit.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void update_framebuffer_size(FramebufferData* data, const ScreenDimensions& screen)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, rep(data->id));

            // Destroy all of the old textures.
            delete_textures(data->attachments);
            delete_texture(data->depth_attachment);

            setup_framebuffer_texture_attachments(data, screen);
        }

        void screen_update(const ScreenDimensions& screen)
        {
            for (auto& data : framebuffer_collection)
            {
                update_framebuffer_size(&data, screen);
            }
            // Bind to the default frame buffer on exit.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        enum class TextureUnit : GLuint
        {
            Sentinel = sentinel_for<TextureUnit>
        };

        RenderTexture alloc_render_texture()
        {
            render_texture_allocator.emplace_front();
            RenderTextureData* tex = &render_texture_allocator.front();
            RenderTexture handle = RenderTexture{ reinterpret_cast<size_t>(tex) };
            return handle;
        }

        RenderTextureData* render_texture_data(RenderTexture tex)
        {
#ifndef NDEBUG
            // Ensure that the node is in the list.
            RenderTextureData* data = reinterpret_cast<RenderTextureData*>(rep(tex));
            bool found = false;
            for (RenderTextureData& e : render_texture_allocator)
            {
                if (&e == data)
                {
                    found = true;
                }
            }
            assert(found);
#endif
            return reinterpret_cast<RenderTextureData*>(rep(tex));
        }

        void dealloc_render_texture(RenderTexture tex)
        {
            RenderTextureData* data = render_texture_data(tex);
            ListHelpers::remove_list_element(&render_texture_allocator, data);
        }

        void init_render_texture(RenderTextureData* data, const ScreenDimensions& screen)
        {
            data->size = screen;
            init_framebuffer(&data->data, screen);
        }

        void update_render_texture(RenderTextureData* data, const ScreenDimensions& screen)
        {
            data->size = screen;
            update_framebuffer_size(&data->data, screen);
        }

        void delete_render_texture(RenderTextureData* data)
        {
            delete_textures(data->data.attachments);
            delete_texture(data->data.depth_attachment);
            glDeleteFramebuffers(1, reinterpret_cast<GLuint*>(&data->data.id));
        }
    } // namespace [anon]

    Camera cursor_camera_transform(const Camera& old_camera,
                                    Vec2f target,
                                    float target_scale_x,
                                    float zoom_factor_x,
                                    float delta_time)
    {
        Camera camera = old_camera;
        // Note: someday we may also change the y scale factor (which would require a corresponding
        // shader change), but not today.
        if (target_scale_x > Constants::max_camera_zoom)
        {
            target_scale_x = Constants::max_camera_zoom;
        }
        // Sometimes the camera will be set to a scale of 0.f to indicate that we're manually zooming.
        else if (camera.scale.x != 0.f)
        {
            float offset_x = target.x - zoom_factor_x/camera.scale.x;
            if (offset_x < 0.f)
            {
                offset_x = 0.f;
            }
            target.x = zoom_factor_x/camera.scale.x + offset_x;
        }

        // Let's try these faster values for a bit...
        camera.velocity = (target - camera.pos) * Vec2f(15.f);
        camera.scale_velocity.x = ((target_scale_x) - camera.scale.x) * 10.f;

        camera.pos = camera.pos + (camera.velocity * Vec2f(delta_time));
        camera.scale = camera.scale + camera.scale_velocity * delta_time;
        return camera;
    }

    WorldCamera cursor_camera_transform(const WorldCamera& old_camera,
                                    Vec2d target,
                                    double target_scale_x,
                                    double zoom_factor_x,
                                    float delta_time)
    {
        WorldCamera camera = old_camera;
        // Note: someday we may also change the y scale factor (which would require a corresponding
        // shader change), but not today.
        if (target_scale_x > Constants::max_camera_zoom)
        {
            target_scale_x = Constants::max_camera_zoom;
        }
        // Sometimes the camera will be set to a scale of 0. to indicate that we're manually zooming.
        else if (camera.scale.x != 0.)
        {
            double offset_x = target.x - zoom_factor_x/camera.scale.x;
            if (offset_x < 0.)
            {
                offset_x = 0.;
            }
            target.x = zoom_factor_x/camera.scale.x + offset_x;
        }

        // Let's try these faster values for a bit...
        camera.velocity = (target - camera.pos) * Vec2d(15.);
        camera.scale_velocity.x = ((target_scale_x) - camera.scale.x) * 10.;

        camera.pos = camera.pos + (camera.velocity * Vec2d(delta_time));
        camera.scale = camera.scale + camera.scale_velocity * delta_time;
        return camera;
    }

    Vec2f screen_to_world_transform(const Camera& camera,
                                    Vec2f point,
                                    const ScreenDimensions& screen)
    {
        // 'point' is assumed to be in screen coordinates.  In order to translate this to world
        // coordinates based on a specific camera, we need to compute the x/y plane coords first.
        const float x_coord = (2.f * (static_cast<float>(point.x) / rep(screen.width) - 0.f)) - 1.f;
        const float y_coord = 1.f - (2.f * (static_cast<float>(point.y) / rep(screen.height) - 0.f));

        // Now we perform the inverse of the vertex shader transform (see transform.vert for reference)
        // and offset by the camera offset.
        // Further note: we only populate the 'x' on the scale since we only scale by that factor for now.
        // See 'cursor_camera_transform'.
        point.x = camera.pos.x + ((x_coord * rep(screen.width)) / (Constants::shader_scale_factor * camera.scale.x));
        point.y = camera.pos.y + ((y_coord * rep(screen.height)) / (Constants::shader_scale_factor * camera.scale.x));
        return point;
    }

    RenderViewport RenderViewport::basic(const ScreenDimensions& screen)
    {
        return { .offset_x = ViewportOffsetX{ },
                    .offset_y = ViewportOffsetY{ },
                    .width = screen.width,
                    .height = screen.height };
    }

    ScopedRenderViewport::ScopedRenderViewport(RenderViewport old, SceneRenderer* renderer):
        current{ old }, old_viewport{ old }, renderer{ renderer } { }

    ScopedRenderViewport::~ScopedRenderViewport()
    {
        reset_viewport();
    }

    void ScopedRenderViewport::apply_viewport(RenderViewport viewport)
    {
        // This ensures that pixels snap to an even number.
        current = viewport;
        float w = static_cast<float>(current.width);
        if ((rep(current.width) & 1) == 1)
        {
            w += 1.0;
            current.width = extend(current.width);
        }
        float h = static_cast<float>(current.height);
        if ((rep(current.height) & 1) == 1)
        {
            h += 1.0;
            current.height = extend(current.height);
        }
        glViewport(rep(current.offset_x),
                    rep(current.offset_y),
                    rep(current.width),
                    rep(current.height));
        renderer->resolution(Vec2f(w, h));
    }

    void ScopedRenderViewport::reset_viewport()
    {
        apply_viewport(old_viewport);
    }

    ScopedRenderViewport ScopedRenderViewport::sub() const
    {
        return { current, renderer };
    }

    ScopedRenderViewportScissor::ScopedRenderViewportScissor(RenderViewport old):
        current{ old }, old_viewport{ old }, old_scissor{ !!glIsEnabled(GL_SCISSOR_TEST) } { }

    ScopedRenderViewportScissor::~ScopedRenderViewportScissor()
    {
        reset_viewport();
    }

    void ScopedRenderViewportScissor::apply_viewport(RenderViewport viewport)
    {
        current = viewport;
        glEnable(GL_SCISSOR_TEST);
        glViewport(rep(current.offset_x),
                    rep(current.offset_y),
                    // We retain the resolution of the original viewport.
                    rep(old_viewport.width),
                    rep(old_viewport.height));
        // Apply scissor.
        glScissor(rep(current.offset_x),
                    rep(current.offset_y),
                    rep(current.width),
                    rep(current.height));
    }

    void ScopedRenderViewportScissor::reset_viewport()
    {
        apply_viewport(old_viewport);
        if (old_scissor)
        {
            glEnable(GL_SCISSOR_TEST);
        }
        else
        {
            glDisable(GL_SCISSOR_TEST);
        }
    }

    ScissorRegion ScissorRegion::basic(const ScreenDimensions& screen)
    {
        return { .offset_x = ScissorOffsetX{ },
                    .offset_y = ScissorOffsetY{ },
                    .width = screen.width,
                    .height = screen.height };
    }

    ScopedScissorRegion::~ScopedScissorRegion()
    {
        remove_scissor();
    }

    void ScopedScissorRegion::apply_scissor(const ScissorRegion& region)
    {
        enable_scissor();
        glScissor(rep(region.offset_x),
                    rep(region.offset_y),
                    rep(region.width),
                    rep(region.height));
    }

    void ScopedScissorRegion::enable_scissor()
    {
        glEnable(GL_SCISSOR_TEST);
    }

    void ScopedScissorRegion::remove_scissor()
    {
        glDisable(GL_SCISSOR_TEST);
    }

    struct SceneRenderer::Data
    {
        FragShader selected_frag_shader = FragShader::BasicColor;
        VertShader selected_vert_shader = VertShader::CameraTransform;

        UniformsContainer uniforms{};

        Vec2f resolution;
        float time = 0.f;
        float dt = 0.f;
        float custom_float_value1 = 0.f;
        float custom_float_value2 = 0.f;
        Vec2f custom_vec2_value1;
        Vec2f custom_vec2_value2;
        Vec2f custom_vec2_value3;
        TextureUnit previous_texture = TextureUnit::Sentinel;

        Camera camera;
    };

    // Mostly rendering stuff...
    namespace
    {
        void dummy_cull(SceneRenderer*) { }
        void cull_vertices(SceneRenderer* renderer)
        {
            renderer->flush();
        }

        using Culler = void(*)(SceneRenderer*);

        Culler cullers[] = {
            cull_vertices,
            dummy_cull
        };

        void render_vertex(SceneRenderer* renderer, const RenderVertex& target)
        {
            assert(vertices_flush_count < vertex_cap);
            vertices[vertices_flush_count] = target;
            ++vertices_flush_count;
            // This function is extremely hot, so we need to reduce the number of branches as
            // much as humanly possible.  Below we implement a branchless dispatch table to
            // identify when the vertex count grows large enough to cull.
            // Note: since vertices_flush_count will always be at least '1' at this point, we
            // will only make the following boolean expression 'true' exactly one time, when
            // vertices_flush_count == vertex_cap.
            cullers[bool(vertices_flush_count % vertex_cap)](renderer);
        }

        // 2
        // | \ 
        // 0 - 1
        void render_triangle(SceneRenderer* renderer,
                            const Vec2f& p0, const Vec2f& p1, const Vec2f& p2,
                            const Vec4f& c0, const Vec4f& c1, const Vec4f& c2,
                            const Vec2f& uv0, const Vec2f& uv1, const Vec2f& uv2)
        {
            render_vertex(renderer, { .pos = p0, .color = c0, .uv = uv0 });
            render_vertex(renderer, { .pos = p1, .color = c1, .uv = uv1 });
            render_vertex(renderer, { .pos = p2, .color = c2, .uv = uv2 });
        }

        // 2 - 3
        // | \ |
        // 0 - 1
        void render_quad(SceneRenderer* renderer,
                        const Vec2f& p0, const Vec2f& p1, const Vec2f& p2, const Vec2f& p3,
                        const Vec4f& c0, const Vec4f& c1, const Vec4f& c2, const Vec4f& c3,
                        const Vec2f& uv0, const Vec2f& uv1, const Vec2f& uv2, const Vec2f& uv3)
        {
            render_triangle(renderer, p0, p1, p2, c0, c1, c2, uv0, uv1, uv2);
            render_triangle(renderer, p1, p2, p3, c1, c2, c3, uv1, uv2, uv3);
        }
    } // namespace [anon]

    SceneRenderer::SceneRenderer():
        data{ new Data } { }

    SceneRenderer::~SceneRenderer() = default;

    bool SceneRenderer::init(const ScreenDimensions& screen)
    {
        init_vertex_buffer();
        for (auto& framebuf : framebuffer_collection)
        {
            init_framebuffer(&framebuf, screen);
        }

        for (int v = 0; v != count_of<VertShader>; ++v)
        {
            auto vert_handle = compile_shader_file(builtin_vert_shader_path(VertShader{ v }), Glew::ShaderType::Vertex, default_reporter);
            if (not vert_handle)
                return false;
            for (int f = 0; f != count_of<FragShader>;++f)
            {
                auto frag_handle = compile_shader_file(builtin_frag_shader_path(FragShader{ f }), Glew::ShaderType::Fragment, default_reporter);
                if (not frag_handle)
                    return false;
                shader_programs[v][f] = Glew::attach_and_create_program(
                    Glew::VertexShaderHandle{ vert_handle.handle() },
                    Glew::FragmentShaderHandle{ frag_handle.handle() });
                if (!Glew::link_program(shader_programs[v][f].handle(), default_reporter))
                    return false;
            }
        }
        return true;
    }

    void SceneRenderer::set_shader(FragShader shader)
    {
        data->selected_frag_shader = shader;
        glUseProgram(rep(shader_programs[rep(data->selected_vert_shader)][rep(shader)].handle()));
        populate_uniform_locations(shader_programs[rep(data->selected_vert_shader)][rep(shader)].handle(), &data->uniforms);
        glUniform2f(rep(data->uniforms[rep(ShaderUniformLocation::Resolution)]), data->resolution.x, data->resolution.y);
        glUniform1f(rep(data->uniforms[rep(ShaderUniformLocation::Time)]), data->time);
        glUniform1f(rep(data->uniforms[rep(ShaderUniformLocation::CameraCoordFactor)]), Constants::shader_scale_factor);
        glUniform2f(rep(data->uniforms[rep(ShaderUniformLocation::CameraPos)]), data->camera.pos.x, data->camera.pos.y);
        glUniform2f(rep(data->uniforms[rep(ShaderUniformLocation::CameraScale)]), data->camera.scale.x, data->camera.scale.y);
        glUniform1f(rep(data->uniforms[rep(ShaderUniformLocation::CustomFloatValue1)]), data->custom_float_value1);
        glUniform1f(rep(data->uniforms[rep(ShaderUniformLocation::CustomFloatValue2)]), data->custom_float_value2);
        glUniform2f(rep(data->uniforms[rep(ShaderUniformLocation::CustomVec2Value1)]), data->custom_vec2_value1.x, data->custom_vec2_value1.y);
        glUniform2f(rep(data->uniforms[rep(ShaderUniformLocation::CustomVec2Value2)]), data->custom_vec2_value2.x, data->custom_vec2_value2.y);
        glUniform2f(rep(data->uniforms[rep(ShaderUniformLocation::CustomVec2Value3)]), data->custom_vec2_value3.x, data->custom_vec2_value3.y);
        if (data->previous_texture != TextureUnit::Sentinel)
        {
            // Now we can bind it to texture unit 1.
            glActiveTexture(GL_TEXTURE1);
            bind_texture(rep(data->previous_texture));
            // We also keep texture unit 0 as the active texture unit for future binding since the bind above
            // is a 1-off thing.
            glActiveTexture(GL_TEXTURE0);
            // Set the uniform properly.
            // NOTE: The second parameter is NOT the texture id but rather the unit to which the texture is
            // associated.  In the case of this texture we used GL_TEXTURE1 so the unit is 1.
            glUniform1i(rep(data->uniforms[rep(ShaderUniformLocation::PreviousPassTexture)]), 1);
        }
    }

    void SceneRenderer::set_shader(VertShader shader)
    {
        // Since the vertex shader always requires a fragment shader, we won't bother setting the uniform locations
        // just yet.
        data->selected_vert_shader = shader;
    }

    ScopedRenderViewport SceneRenderer::create_viewport(const ScreenDimensions& screen)
    {
        // Perhaps we should discard the 'screen' argument and simply use glGet to get these properties, but most
        // of the time we know them so we can save the query time.
        return { RenderViewport::basic(screen), this };
    }

    ScopedRenderViewport SceneRenderer::create_viewport(const RenderViewport& viewport)
    {
        // Still possibly use glGet to do this...
        return { viewport, this };
    }

    ScopedRenderViewportScissor SceneRenderer::create_scissor_viewport(const ScreenDimensions& screen)
    {
        // Perhaps we should discard the 'screen' argument and simply use glGet to get these properties, but most
        // of the time we know them so we can save the query time.
        return { RenderViewport::basic(screen) };
    }

    ScopedRenderViewportScissor SceneRenderer::create_scissor_viewport(const RenderViewport& viewport)
    {
        // Still possibly use glGet to do this...
        return { viewport };
    }

    void SceneRenderer::populate_buffer()
    {
        glBufferSubData(GL_ARRAY_BUFFER,
                        0,
                        vertices_flush_count * sizeof(RenderVertex),
                        vertices);
    }

    void SceneRenderer::draw()
    {
        glDrawArrays(GL_TRIANGLES, 0, vertices_flush_count);
    }

    void SceneRenderer::flush()
    {
        populate_buffer();
        draw();
        vertices_flush_count = 0;
#ifndef NDEBUG
        current_renderer = nullptr;
#endif // NDEBUG
    }

    void SceneRenderer::solid_rect(const Vec2f& top_left, const Vec2f& size, const Vec4f& color)
    {
        assert(current_renderer == nullptr or current_renderer == this);
#ifndef NDEBUG
        current_renderer = this;
#endif // NDEBUG
        constexpr Vec2f top_left_uv{-1.f, 1.f};
        constexpr Vec2f bottom_left_uv{-1.f, -1.f};
        constexpr Vec2f top_right_uv{1.f, 1.f};
        constexpr Vec2f bottom_right_uv{1.f, -1.f};
        render_quad(this,
            top_left,
            top_left + Vec2f(size.x, 0),
            top_left + Vec2f(0, size.y),
            top_left + size,
            // Color
            color,
            color,
            color,
            color,
            // UV transform is empty
            top_left_uv,
            top_right_uv,
            bottom_left_uv,
            bottom_right_uv);
    }

    void SceneRenderer::render_image(const Vec2f& pos, const Vec2f& size, const Vec2f& uv_pos, const Vec2f& uv_size, const Vec4f& color)
    {
        assert(current_renderer == nullptr or current_renderer == this);
#ifndef NDEBUG
        current_renderer = this;
#endif // NDEBUG
        render_quad(this,
            pos,
            pos + Vec2f(size.x, 0),
            pos + Vec2f(0, size.y),
            pos + size,
            // Color
            color,
            color,
            color,
            color,
            // UV transform
            uv_pos,
            uv_pos + Vec2f(uv_size.x, 0),
            uv_pos + Vec2f(0, uv_size.y),
            uv_pos + uv_size);
    }

    void SceneRenderer::strike_rect(const Vec2f& top_left, const Vec2f& size, float thickness, const Vec4f& color)
    {
        auto strike_pos = top_left;
        auto strike_size = size;
        //      A
        //   ----------
        //   |        |
        // D |        | B
        //   |        |
        //   ----------
        //     C
        //
        // A
        strike_size.y = thickness;
        solid_rect(strike_pos, strike_size, color);
        // C
        strike_pos.y = top_left.y + size.y - thickness;
        solid_rect(strike_pos, strike_size, color);
        // D
        strike_pos.y = top_left.y + thickness;
        strike_size.y = size.y - thickness * 2.f;
        strike_size.x = thickness;
        solid_rect(strike_pos, strike_size, color);
        // B
        strike_pos.x = top_left.x + size.x - thickness;
        solid_rect(strike_pos, strike_size, color);
    }

    void SceneRenderer::solid_circle(const Vec2f& center, float radius, const Vec4f& color)
    {
        // radius = 3
        // 
        //             A
        //         ---------
        //         |       |
        //         |       |
        //       D |   *   | B
        //         |       |
        //         |       |
        //         ---------
        //             C
        Vec2f top_left = center - radius;
        Vec2f size{ radius * 2.f };
        solid_rect(top_left, size, color);
    }

    void SceneRenderer::line(const Vec2f& a, const Vec2f& b, float thickness, const Vec4f& color)
    {
        render_vertex(this, { .pos = a, .color = color, .uv = {} });
        render_vertex(this, { .pos = b, .color = color, .uv = {} });
        populate_buffer();
        glEnable(GL_LINE_SMOOTH);
        glLineWidth(thickness);
        glDrawArrays(GL_LINE_STRIP, 0, vertices_flush_count);
        vertices_flush_count = 0;
#ifndef NDEBUG
        current_renderer = nullptr;
#endif // NDEBUG
    }

    const Camera& SceneRenderer::camera() const
    {
        return data->camera;
    }

    void SceneRenderer::camera(const Camera& new_camera)
    {
        data->camera = new_camera;
    }

    void SceneRenderer::resolution(const Vec2f& res)
    {
        data->resolution = res;
    }

    const Vec2f& SceneRenderer::resolution() const
    {
        return data->resolution;
    }

    void SceneRenderer::update_time(float time)
    {
        // When we finally wrap the time, don't let 'dt' go negative.
        data->dt = std::abs(time - data->time);
        data->time = time;
    }

    float SceneRenderer::time() const
    {
        return data->time;
    }

    float SceneRenderer::delta_time() const
    {
        return data->dt;
    }

    void SceneRenderer::custom_float_value1(float value)
    {
        data->custom_float_value1 = value;
    }

    void SceneRenderer::custom_float_value2(float value)
    {
        data->custom_float_value2 = value;
    }

    void SceneRenderer::custom_vec2_value1(const Vec2f& value)
    {
        data->custom_vec2_value1 = value;
    }

    void SceneRenderer::custom_vec2_value2(const Vec2f& value)
    {
        data->custom_vec2_value2 = value;
    }

    void SceneRenderer::custom_vec2_value3(const Vec2f& value)
    {
        data->custom_vec2_value3 = value;
    }

    void SceneRenderer::reload_shaders(std::string_view asset_core_path, Feed::MessageFeed* feed)
    {
        auto reporter = [&](const std::string& s)
        {
            feed->queue_error(s);
        };
        // Populate the new programs into a temporary container that we can move from later. If
        // shader compilation or linking fails, we leave this function before the new programs are
        // populated.
        ShaderProgramContainer new_programs;
        for (int v = 0; v != count_of<VertShader>; ++v)
        {
            auto shader_path = combine_paths(asset_core_path.data(), builtin_vert_shader_path(VertShader{ v }));
            auto vert_handle = compile_shader_file(shader_path.c_str(), Glew::ShaderType::Vertex, reporter);
            if (not vert_handle)
                return;
            for (int f = 0; f != count_of<FragShader>;++f)
            {
                shader_path = combine_paths(asset_core_path.data(), builtin_frag_shader_path(FragShader{ f }));
                auto frag_handle = compile_shader_file(shader_path.c_str(), Glew::ShaderType::Fragment, reporter);
                if (not frag_handle)
                    return;
                new_programs[v][f] = Glew::attach_and_create_program(
                    Glew::VertexShaderHandle{ vert_handle.handle() },
                    Glew::FragmentShaderHandle{ frag_handle.handle() });
                if (!Glew::link_program(new_programs[v][f].handle(), reporter))
                    return;
            }
        }

        // Success!  Let's move them all over.
        for (int v = 0; v != count_of<VertShader>; ++v)
        {
            std::move(std::begin(new_programs[v]), std::end(new_programs[v]), std::begin(shader_programs[v]));
        }
        feed->queue_info("Shaders reloaded.");
    }

    // Global functions for interacting with the framebuffer.
    void SceneRenderer::screen_resize(const ScreenDimensions& screen)
    {
        screen_update(screen);
    }

    void SceneRenderer::bind_framebuffer(Framebuffer idx)
    {
        // We should only be binding to other framebuffers.  User 'unbind_framebuffer' to get back
        // to the default render buffer.
        auto& framebuf = framebuffer_collection[rep(idx)];
        glBindFramebuffer(GL_FRAMEBUFFER, rep(framebuf.id));
    }

    void SceneRenderer::unbind_framebuffer()
    {
        data->previous_texture = TextureUnit::Sentinel;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void SceneRenderer::enable_prev_pass_texture(Framebuffer prev)
    {
        data->previous_texture = TextureUnit(framebuffer_collection[rep(prev)]
                                    .attachments[rep(ColorAttachments::Default)]);
    }

    void SceneRenderer::enable_prev_pass_texture(RenderTexture prev)
    {
        RenderTextureData* tex_data = render_texture_data(prev);
        data->previous_texture = TextureUnit(tex_data->data.attachments[rep(ColorAttachments::RGBA)]);
    }

    void SceneRenderer::render_framebuffer(const ScreenDimensions& screen, Framebuffer src)
    {
        auto& framebuf = framebuffer_collection[rep(src)];
        bind_texture(framebuf.attachments[rep(ColorAttachments::Default)]);
        auto width = rep(screen.width);
        auto height = rep(screen.height);
        render_image(Vec2f(-width + 0.f, -height + 0.f),
                                Vec2f(width * 2.f, height * 2.f),
                                Vec2f(0.f, 0.f),
                                Vec2f(1.f, 1.f),
                                hex_to_vec4f(0xFFFFFFFF));
        flush();
    }

    void SceneRenderer::bind_framebuffer_texture(Framebuffer src)
    {
        auto& framebuf = framebuffer_collection[rep(src)];
        bind_texture(framebuf.attachments[rep(ColorAttachments::Default)]);
    }

    void SceneRenderer::render_framebuffer_layer(FramebufferIO io, FragShader shader, const ScreenDimensions& full_screen)
    {
        bind_framebuffer(io.dest);
        // Clear this framebuffer completely.
        reset_current_buffer(hex_to_vec4f(0x00000000));
        // We assume that 'src' has its alpha pre-blended.
        apply_blending_mode(Render::BlendingMode::PremultipliedAlpha);
        set_shader(shader);
        render_framebuffer(full_screen, io.src);
    }

    void SceneRenderer::render_framebuffer_layer_noclear(FramebufferIO io, FragShader shader, const ScreenDimensions& full_screen)
    {
        bind_framebuffer(io.dest);
        // We assume that 'src' has its alpha pre-blended.
        apply_blending_mode(Render::BlendingMode::PremultipliedAlpha);
        set_shader(shader);
        render_framebuffer(full_screen, io.src);
    }

    // Various buffer operations.
    void SceneRenderer::reset_current_buffer(const Vec4f& color)
    {
        glClearColor(color.x, color.y, color.z, color.a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void SceneRenderer::apply_blending_mode(BlendingMode mode)
    {
        switch (mode)
        {
        case BlendingMode::PremultipliedAlpha:
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendingMode::SrcAlpha:
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendingMode::Default:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        }
    }

    void draw_background(SceneRenderer* renderer, const ScreenDimensions& screen, const Vec4f& color)
    {
        // Set the appropriate vertex and fragment shaders.
        renderer->set_shader(Render::VertShader::NoTransform);
        renderer->set_shader(Render::FragShader::BasicColor);

        // Span the entire screen.
        Vec2f bg = Vec2f(-rep(screen.width) + 0.f, rep(screen.height) + 0.f);
        Vec2f bg_size = Vec2f((rep(screen.width) * 2 + 0.f), -(rep(screen.height) * 2 + 0.f));
        renderer->solid_rect(bg, bg_size, color);
        renderer->flush();
    }

    // Functions for creating render textures and rendering them.
    RenderTexture SceneRenderer::create_render_texture(const ScreenDimensions& screen)
    {
        // Allocate a new texture.
        RenderTexture tex = alloc_render_texture();
        RenderTextureData* tex_data = render_texture_data(tex);

        // Setup.
        init_render_texture(tex_data, screen);

        return tex;
    }

    void SceneRenderer::bind_render_texture(RenderTexture tex)
    {
        RenderTextureData* tex_data = render_texture_data(tex);
        glBindFramebuffer(GL_FRAMEBUFFER, rep(tex_data->data.id));
    }

    void SceneRenderer::render_render_texture(RenderTexture tex)
    {
        RenderTextureData* tex_data = render_texture_data(tex);
        bind_texture(tex_data->data.attachments[rep(ColorAttachments::Default)]);
        auto width = rep(tex_data->size.width);
        auto height = rep(tex_data->size.height);
        render_image(Vec2f(0.f, 0.f),
                                Vec2f(width + 0.f, height + 0.f),
                                Vec2f(0.f, 0.f),
                                Vec2f(1.f, 1.f),
                                hex_to_vec4f(0xFFFFFFFF));
        flush();
    }

    void SceneRenderer::render_framebuffer_to_render_texture(Framebuffer src, RenderTexture dest, FragShader shader, const ScreenDimensions& screen)
    {
        // Bind to the texture.
        bind_render_texture(dest);
        // Note: In order to draw the alpha layer properly for this text, we first need to draw it as though the alpha layer
        // were premultiplied itself (e.g. GL_ONE for alpha): https://stackoverflow.com/a/18497511.  We then draw the fully
        // premultiplied version in 'render_editor_text_texture'.
        apply_blending_mode(Render::BlendingMode::PremultipliedAlpha);
        reset_current_buffer(hex_to_vec4f(0x00000000));

        set_shader(Render::VertShader::OneOneTransform);
        set_shader(shader);
        // Setup the image we're going to sample from.
        bind_framebuffer_texture(src);
        float width = static_cast<float>(rep(screen.width));
        float height = static_cast<float>(rep(screen.height));
        // Note: we always use the size of the framebuffer and rely on the fact that OpenGL
        // will chop samples for us.
        render_image(Vec2f(0.f, 0.f),
                                Vec2f(width, height),
                                Vec2f(0.f, 0.f),
                                Vec2f(1.f, 1.f),
                                hex_to_vec4f(0xFFFFFFFF));
        flush();
    }

    void SceneRenderer::update_render_texture(RenderTexture tex, const ScreenDimensions& screen)
    {
        RenderTextureData* tex_data = render_texture_data(tex);
        ::Render::update_render_texture(tex_data, screen);
    }

    void SceneRenderer::delete_render_texture(RenderTexture tex)
    {
        RenderTextureData* tex_data = render_texture_data(tex);
        ::Render::delete_render_texture(tex_data);
        dealloc_render_texture(tex);
    }


    // Functions for creating basic textures and manipulating them.
    BasicTexture SceneRenderer::create_basic_texture(const ScreenDimensions& size)
    {
        auto tex = BasicTexture{ create_texture() };
        bind_basic_texture(tex);

        // Attribute the texture.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Set alignment.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        // Generate.
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            rep(internal_texture_format_for_attachment(ColorAttachments::RGBA)),
            rep(size.width),
            rep(size.height),
            0,
            rep(texture_format_for_attachment(ColorAttachments::RGBA)),
            GL_UNSIGNED_BYTE,
            nullptr);

        return tex;
    }

    void SceneRenderer::bind_basic_texture(BasicTexture tex)
    {
        bind_texture(rep(tex));
    }

    void SceneRenderer::delete_basic_texture(BasicTexture tex)
    {
        delete_texture(rep(tex));
    }

    void SceneRenderer::submit_basic_texture_data(BasicTexture tex, BasicTextureEntry entry)
    {
        bind_basic_texture(tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            rep(entry.offset_x),
            rep(entry.offset_y),
            rep(entry.width),
            rep(entry.height),
            rep(texture_format_for_attachment(ColorAttachments::RGBA)),
            GL_UNSIGNED_BYTE,
            entry.buffer);
    }

    // Functions for creating glyph cache textures, binding, and manipulating them.
    GlyphTexture SceneRenderer::create_glyph_texture(const ScreenDimensions& dim)
    {
        // Hardcode this for now.
        glActiveTexture(GL_TEXTURE0);
        GLuint texture;
        glGenTextures(1, &texture);
        auto handle = GlyphTexture{ texture };
        bind_glyph_texture(handle);

        // Attribute the texture.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Set alignment.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        // Generate.
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            // Note: the choice to convert the, traditionally, grayscale bitmap to 'red'
            // is an arbitrary.  It could be any color, as long as we pull the correct
            // color out of the vector in the shaders.
            GL_RED,
            rep(dim.width),
            rep(dim.height),
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            NULL);
        return handle;
    }

    void SceneRenderer::bind_glyph_texture(GlyphTexture tex)
    {
        glBindTexture(GL_TEXTURE_2D, rep(tex));
    }

    void SceneRenderer::submit_glyph_data(GlyphTexture tex, GlyphEntry entry)
    {
        bind_glyph_texture(tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            rep(entry.offset_x),
            rep(entry.offset_y),
            rep(entry.width),
            rep(entry.height),
            GL_RED,
            GL_UNSIGNED_BYTE,
            entry.buffer);
    }
} // namespace Render

namespace Render::Effects
{
    void text_glow(FramebufferIO io, SceneRenderer* renderer, const RenderViewport& viewport, const ScreenDimensions& full_screen)
    {
        auto render_viewport = renderer->create_viewport(viewport);
        // Note: we only need to apply the full_screen viewport once until we need to change it later.
        render_viewport.apply_viewport(RenderViewport::basic(full_screen));
        // Framebuffer renders assume the vert shader is NoTransform.
        renderer->set_shader(Render::VertShader::NoTransform);
        // Blur vert.
        // Values for the shader.
        renderer->custom_float_value1(0.03f); // GLOW_FALLOFF.
        renderer->custom_float_value2(8.0); // TAPS.
        renderer->render_framebuffer_layer({ .src = io.src, .dest = Framebuffer::Scratch1 }, FragShader::CRTEasymodeBlurVert, full_screen);

        // Blur horiz.
        renderer->custom_float_value1(0.03f); // GLOW_FALLOFF.
        renderer->custom_float_value2(8.0); // TAPS.
        renderer->render_framebuffer_layer({ .src = Framebuffer::Scratch1, .dest = Framebuffer::Scratch2 }, FragShader::CRTEasymodeBlurHoriz, full_screen);

        // Blend blur + original framebuffer.
        renderer->enable_prev_pass_texture(Framebuffer::Scratch2);
        renderer->render_framebuffer_layer({ .src = io.src, .dest = Framebuffer::Scratch1 }, FragShader::BasicTextureBlend, full_screen);

        // Write out the result to the default framebuffer '0'.
        render_viewport.reset_viewport();
        renderer->render_framebuffer_layer_noclear({ .src = Framebuffer::Scratch1, .dest = io.dest }, FragShader::Image, full_screen);
    }

    void apply_text_glow_to(RenderTexture in, SceneRenderer* renderer, const ScreenDimensions& full_screen)
    {
        auto vp = RenderViewport::basic(full_screen);
        auto render_viewport = renderer->create_viewport(vp);
        // Note: we only need to apply the full_screen viewport once until we need to change it later.
        render_viewport.apply_viewport(vp);
        // Renders from render textures assume OneOneTransform.
        renderer->set_shader(Render::VertShader::OneOneTransform);
        SceneRenderer::bind_render_texture(in);
        // Blur vert.
        // Values for the shader.
        renderer->custom_float_value1(0.03f); // GLOW_FALLOFF.
        renderer->custom_float_value2(8.0); // TAPS.

        renderer->bind_framebuffer(Framebuffer::Scratch1);
        // Clear this framebuffer completely.
        renderer->reset_current_buffer(hex_to_vec4f(0x00000000));
        // We assume that 'src' has its alpha pre-blended.
        renderer->apply_blending_mode(Render::BlendingMode::PremultipliedAlpha);
        renderer->set_shader(FragShader::CRTEasymodeBlurVert);
        renderer->render_render_texture(in);

        // Blur horiz.
        // Note: framebuffer -> framebuffer assumes NoTransform shader.
        renderer->set_shader(Render::VertShader::NoTransform);
        renderer->custom_float_value1(0.03f); // GLOW_FALLOFF.
        renderer->custom_float_value2(8.0); // TAPS.
        renderer->render_framebuffer_layer({ .src = Framebuffer::Scratch1, .dest = Framebuffer::Scratch2 }, FragShader::CRTEasymodeBlurHoriz, full_screen);

        // In order to prevent a scenario where we need 3 framebuffers, we will output the original image
        // to a scratch framebuffer so we can use this framebuffer as input when rendering the final image
        // back to the render texture.
        renderer->apply_blending_mode(Render::BlendingMode::PremultipliedAlpha);
        renderer->bind_framebuffer(Framebuffer::Scratch1);
        renderer->reset_current_buffer(hex_to_vec4f(0x00000000));
        renderer->set_shader(Render::VertShader::OneOneTransform);
        renderer->set_shader(Render::FragShader::Image);
        renderer->render_render_texture(in);

        // Blend blur + original framebuffer.
        // Render back to texture.
        renderer->enable_prev_pass_texture(Framebuffer::Scratch2);
        renderer->render_framebuffer_to_render_texture(Framebuffer::Scratch1, in, FragShader::BasicTextureBlend, full_screen);
    }

    void blur_background(FramebufferIO io, SceneRenderer* renderer, const RenderViewport& viewport, const ScreenDimensions& full_screen)
    {
        auto render_viewport = renderer->create_viewport(viewport);
        // Note: we only need to apply the full_screen viewport once until we need to change it later.
        render_viewport.apply_viewport(RenderViewport::basic(full_screen));

        // Blur vert.
        // Values for the shader.
        renderer->custom_float_value1(0.03f); // GLOW_FALLOFF.
        renderer->custom_float_value2(4.0); // TAPS.
        renderer->render_framebuffer_layer({ .src = io.src, .dest = Framebuffer::Scratch1 }, FragShader::CRTEasymodeBlurVert, full_screen);

        // Blur horiz.
        renderer->custom_float_value1(0.03f); // GLOW_FALLOFF.
        renderer->custom_float_value2(4.0); // TAPS.
        renderer->render_framebuffer_layer({ .src = Framebuffer::Scratch1, .dest = Framebuffer::Scratch2 }, FragShader::CRTEasymodeBlurHoriz, full_screen);

        // Reapply to default framebuffer.
        // Since we're not blending, we want to stomp on the dest framebuffer with a clear.
        render_viewport.reset_viewport();
        renderer->render_framebuffer_layer({ .src = Framebuffer::Scratch2, .dest = io.dest }, FragShader::Image, full_screen);
    }
} // namespace Render::Effects