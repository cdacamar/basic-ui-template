#pragma once

#include <memory>
#include <string_view>

#include "types.h"
#include "vec.h"

namespace Feed
{
    class MessageFeed;
} // namespace Feed

namespace Render
{
    enum class FragShader
    {
        BasicColor,
        SolidCircle,
        Image,
        Text,
        Icon,
        BasicHSV,
        BasicFade,
        BasicTextureBlend,
        CRTWarp,
        CRTEasymode,
        CRTGamemode,
        // Start - multi-pass shaders for CRT-Easymode-Halation
        CRTEasymodeLinearize,  // #1
        CRTEasymodeBlurHoriz,  // #2
        CRTEasymodeBlurVert,   // #3
        CRTEasymodeThresh,     // #4
        CRTEasymodeHalation,   // #5
        // End - multi-pass shaders for CRT-Easymode-Halation
        Count
    };

    enum class VertShader
    {
        CameraTransform,
        NoTransform,
        OneOneTransform,
        Count
    };

    template <typename T>
    struct CameraT
    {
        Vec2T<T> pos;
        Vec2T<T> scale = 3.;
        Vec2T<T> scale_velocity;
        Vec2T<T> velocity;

        bool operator==(const CameraT&) const = default;
    };

    using Camera = CameraT<float>;

    using WorldCamera = CameraT<double>;

    Camera cursor_camera_transform(const Camera& camera,
                                    Vec2f target,
                                    float target_scale_x,
                                    float zoom_factor_x,
                                    float delta_time);

    WorldCamera cursor_camera_transform(const WorldCamera& camera,
                                    Vec2d target,
                                    double target_scale_x,
                                    double zoom_factor_x,
                                    float delta_time);

    Vec2f screen_to_world_transform(const Camera& camera,
                                    Vec2f point,
                                    const ScreenDimensions& screen);

    enum class ViewportOffsetX : int { };
    enum class ViewportOffsetY : int { };

    struct RenderViewport
    {
        ViewportOffsetX offset_x;
        ViewportOffsetY offset_y;
        Width width;
        Height height;

        static RenderViewport basic(const ScreenDimensions& screen);

        bool operator==(const RenderViewport&) const = default;
    };

    class SceneRenderer;

    class ScopedRenderViewport
    {
    public:
        ScopedRenderViewport(RenderViewport old, SceneRenderer* renderer);
        ~ScopedRenderViewport();

        void apply_viewport(RenderViewport viewport);
        void reset_viewport();
        ScopedRenderViewport sub() const;

        const RenderViewport& current_viewport() const
        {
            return current;
        }

    private:
        RenderViewport current;
        RenderViewport old_viewport;
        SceneRenderer* renderer;
    };

    // Similar to the class above, however it will not adjust resolution and instead trim
    // viewports using scissor rects.
    class ScopedRenderViewportScissor
    {
    public:
        ScopedRenderViewportScissor(RenderViewport old);
        ~ScopedRenderViewportScissor();

        void apply_viewport(RenderViewport viewport);
        void reset_viewport();

        const RenderViewport& current_viewport() const
        {
            return current;
        }

    private:
        RenderViewport current;
        RenderViewport old_viewport;
        bool old_scissor;
    };

    enum class ScissorOffsetX : int { };
    enum class ScissorOffsetY : int { };

    struct ScissorRegion
    {
        ScissorOffsetX offset_x;
        ScissorOffsetY offset_y;
        Width width;
        Height height;

        static ScissorRegion basic(const ScreenDimensions& screen);

        bool operator==(const ScissorRegion&) const = default;
    };

    class ScopedScissorRegion
    {
    public:
        ~ScopedScissorRegion();

        void apply_scissor(const ScissorRegion& region);
        void enable_scissor();
        void remove_scissor();
    };

    enum class Framebuffer
    {
        _0,
        Default = _0,
        _1,
        _2,

        // These buffers are never reserved.
        Scratch1 = _1,
        Scratch2 = _2,
        Count
    };

    struct FramebufferIO
    {
        Framebuffer src;
        Framebuffer dest;
    };

    // A texture which is like a framebuffer but more specific to the component.
    enum class RenderTexture : size_t { };

    // A texture to contain a glyph cache.
    enum class GlyphTexture : uint32_t { };

    enum class GlyphOffsetX : int { };
    enum class GlyphOffsetY : int { };

    struct GlyphEntry
    {
        GlyphOffsetX offset_x;
        GlyphOffsetY offset_y;
        Width width;
        Height height;
        const uint8_t* buffer;
    };

    enum class BasicTexture : uint32_t
    {
        Invalid = sentinel_for<BasicTexture>
    };

    enum class BasicTextureOffsetX : int { };
    enum class BasicTextureOffsetY : int { };

    struct BasicTextureEntry
    {
        BasicTextureOffsetX offset_x;
        BasicTextureOffsetY offset_y;
        Width width;
        Height height;
        const uint8_t* buffer;
    };

    // Note: The general strategy for rendering to a framebuffer and rendering that result to another if this
    // framebuffer has alpha channels is to:
    // 1. Render to the framebuffer with default blending enabled.
    // 2. Bind the dest framebuffer.
    // 3. Apply the pre-multiplied alpha blending (as the src framebuffer had its alpha blended once already).
    // 4. Render the src framebuffer to the dest.
    // 5. Reset the blending mode.
    // Advice taken from: https://stackoverflow.com/questions/2171085/opengl-blending-with-previous-contents-of-framebuffer.
    enum class BlendingMode
    {
        PremultipliedAlpha, // GL_ONE, GL_ONE_MINUS_SRC_ALPHA
        SrcAlpha,           // GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA
        Default,            // GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
    };

    // Note: This basic renderer always renders 'up', e.g. a y-coordinate will correspond to the bottom
    // of the render target.
    class SceneRenderer
    {
    public:
        struct Data;

        SceneRenderer();
        ~SceneRenderer();

        // Initialize global data for all renderer instances.
        static bool init(const ScreenDimensions& screen);
        // Reloads all shaders for every renderer instance.
        static void reload_shaders(const std::string_view asset_core_path, Feed::MessageFeed* feed);

        // Functions for interacting with the framebuffer.
        static void screen_resize(const ScreenDimensions& screen);
        void bind_framebuffer(Framebuffer idx);
        // Back to default render buffer.
        void unbind_framebuffer();
        void enable_prev_pass_texture(Framebuffer prev);
        void enable_prev_pass_texture(RenderTexture prev);
        // Note: It is recommended that you unbind the framebuffer first.  We render
        // this with a non-static instance so that we can shaders can be used for
        // possible postprocessing on the resulting framebuffer.
        void render_framebuffer(const ScreenDimensions& screen, Framebuffer src);
        void bind_framebuffer_texture(Framebuffer src);
        // Using framebuffer 'src', render that framebuffer to framebuffer 'dest' using the provided fragment shader.
        // Note: This will make the blend mode sticky, be sure to unset it, if necessary.
        void render_framebuffer_layer(FramebufferIO io, FragShader shader, const ScreenDimensions& full_screen);
        // Similar to the above, but it does not clear framebuffer content first.
        void render_framebuffer_layer_noclear(FramebufferIO io, FragShader shader, const ScreenDimensions& full_screen);

        // Functions for creating render textures and rendering them.
        static RenderTexture create_render_texture(const ScreenDimensions& screen);
        static void bind_render_texture(RenderTexture tex);
        void render_render_texture(RenderTexture tex);
        void render_framebuffer_to_render_texture(Framebuffer src, RenderTexture dest, FragShader shader, const ScreenDimensions& screen);
        static void update_render_texture(RenderTexture tex, const ScreenDimensions& screen);
        static void delete_render_texture(RenderTexture tex);

        // Functions for creating basic textures and manipulating them.
        static BasicTexture create_basic_texture(const ScreenDimensions& size);
        static void bind_basic_texture(BasicTexture tex);
        static void delete_basic_texture(BasicTexture tex);
        static void submit_basic_texture_data(BasicTexture tex, BasicTextureEntry entry);

        // Functions for creating glyph cache textures, binding, and manipulating them.
        static GlyphTexture create_glyph_texture(const ScreenDimensions& dim);
        static void bind_glyph_texture(GlyphTexture tex);
        // Note: This API assumes the texture is bound.
        static void submit_glyph_data(GlyphTexture tex, GlyphEntry entry);

        // User interaction.
        void flush();
        void set_shader(FragShader shader);
        void set_shader(VertShader shader);
        ScopedRenderViewport create_viewport(const ScreenDimensions& screen);
        ScopedRenderViewport create_viewport(const RenderViewport& viewport);
        ScopedRenderViewportScissor create_scissor_viewport(const ScreenDimensions& screen);
        ScopedRenderViewportScissor create_scissor_viewport(const RenderViewport& viewport);

        // Rendering.
        void solid_rect(const Vec2f& top_left, const Vec2f& size, const Vec4f& color);
        void strike_rect(const Vec2f& top_left, const Vec2f& size, float thickness, const Vec4f& color);
        void solid_circle(const Vec2f& center, float radius, const Vec4f& color);
        // Note: Because line is a different kind of primitive, they are flushed immediately.
        void line(const Vec2f& a, const Vec2f& b, float thickness, const Vec4f& color);
        void render_image(const Vec2f& pos, const Vec2f& size, const Vec2f& uv_pos, const Vec2f& uv_size, const Vec4f& color);

        // Various inputs for shaders.
        const Camera& camera() const;
        void camera(const Camera& new_camera);
        void resolution(const Vec2f& res);
        const Vec2f& resolution() const;
        void update_time(float time);
        float time() const;
        float delta_time() const;
        void custom_float_value1(float value);
        void custom_float_value2(float value);
        void custom_vec2_value1(const Vec2f& value);
        void custom_vec2_value2(const Vec2f& value);
        void custom_vec2_value3(const Vec2f& value);

        // Various buffer operations.
        void reset_current_buffer(const Vec4f& color);
        void apply_blending_mode(BlendingMode mode);

    private:
        void gather_vertices();
        void populate_buffer();
        void draw();

        std::unique_ptr<Data> data;
    };

    // Helper functions.
    // Note: This will set the vert and frag shaders so callers need to remember to set their shaders after.
    void draw_background(SceneRenderer* renderer, const ScreenDimensions& screen, const Vec4f& color);

    namespace Effects
    {
        void text_glow(FramebufferIO io, SceneRenderer* renderer, const RenderViewport& viewport, const ScreenDimensions& full_screen);
        void apply_text_glow_to(RenderTexture in, SceneRenderer* renderer, const ScreenDimensions& full_screen);
        void blur_background(FramebufferIO io, SceneRenderer* renderer, const RenderViewport& viewport, const ScreenDimensions& full_screen);
    } // namespace Effects
} // namespace Render