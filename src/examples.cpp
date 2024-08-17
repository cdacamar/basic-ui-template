#include "examples.h"

#include <format>
#include <string_view>
#include <string>

#include "config.h"
#include "util.h"
#include "vec.h"

namespace Examples
{
    void Intro::render(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const ScreenDimensions& screen)
    {
        constexpr auto font_size = Glyph::FontSize{ 32 };
        constexpr float quad_padding = 2.f;
        // The vertex shader will not change.
        renderer->set_shader(Render::VertShader::OneOneTransform);
        auto font_ctx = atlas->render_font_context(font_size);
        // Hello, world!
        {
            renderer->set_shader(Render::FragShader::Text);
            constexpr std::string_view hello = "Hello, World!";
            float len = font_ctx.measure_text(hello).x;
            // Position in middle of screen.
            Vec2f pos;
            pos.x = (rep(screen.width) - len) / 2.f;
            pos.y = (rep(screen.height) - rep(font_size)) / 2.f;
            font_ctx.render_text(renderer, hello, pos, Config::system_colors().default_font_color);
            font_ctx.flush(renderer);
        }

        constexpr float padding = 10.f;

        // Basic shapes / colors.
        Vec2f pos;
        {
            renderer->set_shader(Render::FragShader::Text);
            constexpr std::string_view quads = "Basic quads";
            float len = font_ctx.measure_text(quads).x;
            pos.x = padding;
            pos.y = (rep(screen.height) - padding - rep(font_size));
            font_ctx.render_text(renderer, quads, pos, Config::system_colors().default_font_color);
            font_ctx.flush(renderer);

            // RGB
            renderer->set_shader(Render::FragShader::BasicColor);
            // Slice each quad according to the above text length.
            float slice_x = (len - quad_padding * 2.f) / 3.f;
            Vec2f size{ slice_x, rep(font_size) + 0.f };
            pos.x = padding;
            pos.y -= padding + rep(font_size);
            // R
            {
                renderer->solid_rect(pos, size, hex_to_vec4f(0xFF0000FF));
            }
            pos.x += slice_x + quad_padding;
            // G
            {
                renderer->solid_rect(pos, size, hex_to_vec4f(0x00FF00FF));
            }
            pos.x += slice_x + quad_padding;
            // G
            {
                renderer->solid_rect(pos, size, hex_to_vec4f(0x0000FFFF));
            }
            // Note: Because we didn't switch fragment shaders, we can draw this as a group.
            renderer->flush();
        }

        // Interesting rects.
        {
            renderer->set_shader(Render::FragShader::Text);
            constexpr std::string_view interesting = "Interesting rects";
            float len = font_ctx.measure_text(interesting).x;
            pos.x = padding;
            // Position it below the above quads.
            pos.y -= padding + rep(font_size);
            font_ctx.render_text(renderer, interesting, pos, Config::system_colors().default_font_color);
            font_ctx.flush(renderer);

            // HSV / Strike rect.
            renderer->set_shader(Render::FragShader::BasicHSV);
            // Slice each quad according to the above text length.
            float slice_x = (len - quad_padding) / 2.f;
            Vec2f size{ slice_x, rep(font_size) + 0.f };
            pos.x = padding;
            pos.y -= padding + rep(font_size);
            // HSV
            {
                renderer->solid_rect(pos, size, hex_to_vec4f(0xFFFFFFFF));
                renderer->flush();
            }
            pos.x += slice_x + quad_padding;
            float mixin = 0;
            // Strike
            {
                renderer->set_shader(Render::FragShader::BasicColor);
                constexpr auto first = hex_to_vec4f(0xFF0000FF);
                constexpr auto last = hex_to_vec4f(0x00FF00FF);
                const float diff = (rep(ticks_since_app_start()) / 1000.f) / 5.f;
                mixin = diff - static_cast<int>(diff);
                auto color = lerp(first, last, mixin);
                renderer->solid_rect(pos, size, color);
                renderer->flush();
                renderer->set_shader(Render::FragShader::BasicHSV);
                renderer->strike_rect(pos, size, 2.f, hex_to_vec4f(0x00FF00FF));
                renderer->flush();
            }

            pos.x += slice_x + quad_padding;
            {
                renderer->set_shader(Render::FragShader::Text);
                std::string txt = std::format("mixin: {:.2f}", mixin);
                pos.y -= (size.y - font_ctx.current_font_line_height()) / 2.f;
                font_ctx.render_text(renderer, txt, pos, Config::system_colors().default_font_color);
                font_ctx.flush(renderer);
            }
        }

        // Circles.
        {
            renderer->set_shader(Render::FragShader::Text);
            constexpr std::string_view circles = "Circles";
            float len = font_ctx.measure_text(circles).x;
            pos.x = padding;
            // Position it below the above quads.
            pos.y -= padding + rep(font_size);
            font_ctx.render_text(renderer, circles, pos, Config::system_colors().default_font_color);
            font_ctx.flush(renderer);

            renderer->set_shader(Render::FragShader::SolidCircle);
            // Position it below the above quads.
            float radius = (len - quad_padding) / 4.f;
            pos.y -= padding + radius;
            pos.x = padding + radius;
            renderer->solid_circle(pos, radius, hex_to_vec4f(0xC586C0FF));

            pos.x += radius * 2.f + quad_padding;
            renderer->solid_circle(pos, radius, hex_to_vec4f(0x569CD6FF));
            renderer->flush();
        }
    }

    namespace
    {
        struct DragNSnapUIData
        {
            Vec2i last_mouse_down_start;
            bool dragging = false;
        };
    } // namespace [anon]

    struct DragNSnap::Data
    {
        static constexpr float track_thickness = 2.f;
        static constexpr float padding = 2.f;
        static constexpr float radius = 10.f;
        static constexpr Glyph::FontSize font_size = Glyph::FontSize{ 18 };

        Vec2f movement_offset_exp = 0.f;
        Vec2f movment_offset_lerp = 0.f;
        Vec2f movment_offset_linear = 0.f;
        Vec2f ball_pos = 0.f;
        DragNSnapUIData ui_data;
    };

    namespace
    {
        bool drag_n_snap_dragging(const DragNSnap::Data& data)
        {
            return data.ui_data.dragging;
        }

        bool drag_n_snap_should_begin_drag(const DragNSnap::Data& data)
        {
            return not drag_n_snap_dragging(data);
        }

        void drag_n_snap_begin_drag(DragNSnap::Data* data)
        {
            data->ui_data.dragging = true;
            data->movement_offset_exp = 0.f;
            data->movment_offset_lerp = 0.f;
            data->movment_offset_linear = 0.f;
        }

        void drag_n_snap_process_mouse_move_drag(DragNSnap::Data* data, const Vec2i& mouse_pos)
        {
            data->ball_pos.x = static_cast<float>(mouse_pos.x - data->ui_data.last_mouse_down_start.x);
        }

        void drag_n_snap_end_drag(DragNSnap::Data* data)
        {
            // Add movement offset.
            data->movement_offset_exp.x = data->ball_pos.x;
            data->movment_offset_lerp.x = data->ball_pos.x;
            data->movment_offset_linear.x = data->ball_pos.x;
            data->ball_pos.x = 0.f;
            data->ui_data.dragging = false;
        }

        struct RenderTrackInput
        {
            const Vec2f& ball_pos;
            const Vec2f& offset;
            float midpoint;
            float track_length;
            float track_x;
        };

        void drag_n_snap_render_track(Render::SceneRenderer* renderer, RenderTrackInput in)
        {
            // This is a basic line which spans the middle of the viewport.
            // Draw the track first.
            {
                // This is a basic line which spans the middle of the viewport.
                renderer->set_shader(Render::FragShader::BasicColor);
                Vec2f start{ in.track_x, in.midpoint };
                Vec2f end = { in.track_x + in.track_length, in.midpoint };
                renderer->line(start, end, DragNSnap::Data::track_thickness, hex_to_vec4f(0xCE9178FF));
            }

            // Draw ball.
            {
                renderer->set_shader(Render::FragShader::SolidCircle);
                // The only position we care about is the 'x' position.  We compute the y position
                // each frame.
                Vec2f center{ in.track_x + in.ball_pos.x, in.midpoint };
                // Adjust the 'x' position by the radius (to make it always draw within the box) and
                // by movement offset.
                center.x += DragNSnap::Data::radius + in.offset.x;
                renderer->solid_circle(center, DragNSnap::Data::radius, hex_to_vec4f(0xB5CEA8FF));
                renderer->flush();
            }
        }
    } // namespace [anon]

    DragNSnap::DragNSnap():
        data{ new Data } { }

    DragNSnap::~DragNSnap() = default;

    // UI Interaction.
    void DragNSnap::mouse_down(const UI::UIState& state, const Vec2i& mouse_pos)
    {
        if (not implies(state.mouse, UI::MouseState::LDown))
            return;
        data->ui_data.last_mouse_down_start = mouse_pos;
    }

    void DragNSnap::mouse_up(const UI::UIState&, const Vec2i&)
    {
        if (drag_n_snap_dragging(*data))
        {
            drag_n_snap_end_drag(data.get());
        }
    }

    void DragNSnap::mouse_move(const UI::UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
    {
        if (not implies(state.mouse, UI::MouseState::LDown))
            return;
        if (not UI::mouse_in_viewport(mouse_pos, viewport))
            return;
        if (drag_n_snap_should_begin_drag(*data))
        {
            drag_n_snap_begin_drag(data.get());
        }
        drag_n_snap_process_mouse_move_drag(data.get(), mouse_pos);
    }

    void DragNSnap::render(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const Render::RenderViewport& viewport)
    {
        // This is a basic track with a ball on it.
        // ------*-----
        renderer->set_shader(Render::VertShader::OneOneTransform);
        // Debug rect for viewport.
        {
            renderer->set_shader(Render::FragShader::BasicColor);
            Vec2f left{ 0.f, 0.f };
            Vec2f size{ rep(viewport.width) + 0.f, rep(viewport.height) + 0.f };
            renderer->strike_rect(left, size, 2.f, hex_to_vec4f(0xE3811CFF));
            renderer->flush();
        }

        const float mix_exp = (rep(viewport.height) - Data::padding * 4.f) / 4.f;
        const float mid_linear = mix_exp * 2.f + Data::padding;
        const float upper_linear = mix_exp * 3.f + Data::padding * 2.f;

        // Compute some text attributes first.
        auto font_ctx = atlas->render_font_context(Data::font_size);
        static constexpr std::string_view exp_text = "Exponential:";
        static constexpr std::string_view lerp_text = "Linear Interp:";
        static constexpr std::string_view linear_text = "Linear:";

        // Find the largest text width.
        float max_text_width = std::max({ font_ctx.measure_text(exp_text).x,
                                          font_ctx.measure_text(lerp_text).x,
                                          font_ctx.measure_text(linear_text).x });

        const float track_length = rep(viewport.width) - Data::padding * 3.f - max_text_width;
        const float track_x = Data::padding * 2.f + max_text_width;

        // Exponential easing.
        {
            renderer->set_shader(Render::FragShader::Text);
            Vec2f pos{ Data::padding, mix_exp - font_ctx.current_font_size() / 2.f };
            font_ctx.render_text(renderer, exp_text, pos, Config::system_colors().default_font_color);
            font_ctx.flush(renderer);

            RenderTrackInput in{
                .ball_pos = data->ball_pos,
                .offset = data->movement_offset_exp,
                .midpoint = mix_exp,
                .track_length = track_length,
                .track_x = track_x
            };
            drag_n_snap_render_track(renderer, in);
        }

        // Linear interp easing.
        {
            renderer->set_shader(Render::FragShader::Text);
            Vec2f pos{ Data::padding, mid_linear - font_ctx.current_font_size() / 2.f };
            font_ctx.render_text(renderer, lerp_text, pos, Config::system_colors().default_font_color);
            font_ctx.flush(renderer);

            RenderTrackInput in{
                .ball_pos = data->ball_pos,
                .offset = data->movment_offset_lerp,
                .midpoint = mid_linear,
                .track_length = track_length,
                .track_x = track_x
            };
            drag_n_snap_render_track(renderer, in);
        }

        // Linear movement.
        {
            renderer->set_shader(Render::FragShader::Text);
            Vec2f pos{ Data::padding, upper_linear - font_ctx.current_font_size() / 2.f };
            font_ctx.render_text(renderer, linear_text, pos, Config::system_colors().default_font_color);
            font_ctx.flush(renderer);

            RenderTrackInput in{
                .ball_pos = data->ball_pos,
                .offset = data->movment_offset_linear,
                .midpoint = upper_linear,
                .track_length = track_length,
                .track_x = track_x
            };
            drag_n_snap_render_track(renderer, in);
        }

        // Update the movement offset.
        if (data->movement_offset_exp != 0.f)
        {
            data->movement_offset_exp = ease_expon_val(data->movement_offset_exp, renderer->delta_time(), 3.f);
        }

        if (data->movment_offset_lerp != 0.f)
        {
            data->movment_offset_lerp = lerp(data->movment_offset_lerp, Vec2f(0.f), renderer->delta_time());
        }

        if (data->movment_offset_linear != 0.f)
        {
            const Vec2f speed = track_length / 3.f;
            const float sign = data->movment_offset_linear.x < 0.f ? -1.f : 1.f;
            const float old_x = data->movment_offset_linear.x;
            //float old_x = data->movment_offset_linear.x;
            data->movment_offset_linear = data->movment_offset_linear - sign * speed * renderer->delta_time();
            if (std::abs(data->movment_offset_linear.x) > std::abs(old_x))
            {
                data->movment_offset_linear = 0.f;
            }
        }
    }
} // namespace Examples