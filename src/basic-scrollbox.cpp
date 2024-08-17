#include "basic-scrollbox.h"

#include <algorithm>

#include "config.h"

namespace UI::Widgets
{
    namespace
    {
        struct UIData
        {
            Vec2i last_mouse_down_start;
            Vec2f initial_scroll_offset;
            bool dragging = false;
            bool hover_scroll = false;
        };
    } // namespace [anon]

    struct ScrollBox::Data
    {
        static constexpr int padding = 2;
        static constexpr float scrollbar_width = 10.f;

        Vec2f content_size;
        Vec2f scroll_offset;
        UIData ui_data;
        bool draw_border = false;
    };

    namespace
    {
        struct ScrollbarRect
        {
            Vec2f pos;
            Vec2f size;
        };

        ScrollbarRect scrollbar_box(ScrollBox::Data* data, const Render::RenderViewport& viewport)
        {
            // Note: the viewport height is added to content size height because the viewport goes from y(0) to y(content.height).
            const float height = rep(viewport.height) * (rep(viewport.height) / (data->content_size.y + rep(viewport.height)));
            const float offset_multiplier = data->scroll_offset.y / data->content_size.y;
            Vec2f left{ rep(viewport.width) - ScrollBox::Data::scrollbar_width, 0.f };
            Vec2f size{ ScrollBox::Data::scrollbar_width, 0.f };
            size.y = height;
            left.y = rep(viewport.height) - height - (rep(viewport.height) - height) * offset_multiplier;
            return { .pos = left, .size = size };
        }

        bool dragging(const ScrollBox::Data& data)
        {
            return data.ui_data.dragging;
        }

        bool should_begin_drag(const ScrollBox::Data& data)
        {
            return not dragging(data);
        }

        void begin_drag(ScrollBox::Data* data)
        {
            data->ui_data.dragging = true;
            data->ui_data.initial_scroll_offset = data->scroll_offset;
        }

        void mouse_move_drag(ScrollBox::Data* data, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
        {
            const float height = rep(viewport.height) * rep(viewport.height) / (data->content_size.y + rep(viewport.height));
            const float off = -static_cast<float>(mouse_pos.y - data->ui_data.last_mouse_down_start.y);
            const float off_scaled = data->content_size.y * off / (rep(viewport.height) - height);
            data->scroll_offset.y = std::clamp(data->ui_data.initial_scroll_offset.y + off_scaled, 0.f, data->content_size.y);
        }

        void end_drag(ScrollBox::Data* data, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
        {
            data->ui_data.dragging = false;
            data->ui_data.hover_scroll = false;

            // Test to see if we're hovering the scrollbar.
            auto [pos, size] = scrollbar_box(data, viewport);
            // Adjust the mouse for the viewport.
            auto adjusted_mouse = mouse_pos;
            adjusted_mouse.x -= rep(viewport.offset_x);
            adjusted_mouse.y -= rep(viewport.offset_y);
            if (basic_aabb({ .pos = pos, .size = size }, adjusted_mouse))
            {
                data->ui_data.hover_scroll = true;
            }
        }
    } // namespace [anon]

    ScrollBox::ScrollBox():
        data{ new Data } { }

    ScrollBox::~ScrollBox() = default;

    void ScrollBox::content_size(const Vec2f& size)
    {
        data->content_size = size;
        data->scroll_offset.y = std::clamp(data->scroll_offset.y, 0.f, data->content_size.y);
        data->scroll_offset.x = std::clamp(data->scroll_offset.x, 0.f, data->content_size.x);
    }

    void ScrollBox::draw_border(bool b)
    {
        data->draw_border = b;
    }

    Vec2f ScrollBox::position() const
    {
        return data->scroll_offset;
    }

    Render::RenderViewport ScrollBox::content_viewport(const Render::RenderViewport& viewport) const
    {
        // reduce the margins a bit.
        auto new_viewport = viewport;
        new_viewport.width = Width{ rep(viewport.width) - Data::padding - static_cast<int>(Data::scrollbar_width) };
        new_viewport.height = Height{ rep(viewport.height) - Data::padding * 2 };
        new_viewport.offset_x = Render::ViewportOffsetX{ rep(viewport.offset_x) + Data::padding };
        new_viewport.offset_y = Render::ViewportOffsetY{ rep(viewport.offset_y) + Data::padding };
        return new_viewport;
    }

    // UI Interaction.
    void ScrollBox::scroll_up(float amount, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
    {
        if (not mouse_in_viewport(mouse_pos, viewport))
            return;
        data->scroll_offset.y = std::clamp(data->scroll_offset.y - amount, 0.f, data->content_size.y);
    }

    void ScrollBox::scroll_down(float amount, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
    {
        if (not mouse_in_viewport(mouse_pos, viewport))
            return;
        data->scroll_offset.y = std::clamp(data->scroll_offset.y + amount, 0.f, data->content_size.y);
    }

    void ScrollBox::mouse_down(const UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport&)
    {
        if (not implies(state.mouse, MouseState::LDown))
            return;
        data->ui_data.last_mouse_down_start = mouse_pos;
    }

    void ScrollBox::mouse_up(const UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
    {
        if (dragging(*data) and not implies(state.mouse, MouseState::LDown))
        {
            end_drag(data.get(), mouse_pos, viewport);
        }
    }

    void ScrollBox::mouse_move(const UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
    {
        if (not implies(state.mouse, MouseState::LDown))
        {
            data->ui_data.hover_scroll = false;
            // Test to see if we're hovering the scrollbar.
            auto [pos, size] = scrollbar_box(data.get(), viewport);
            // Adjust the mouse for the viewport.
            auto adjusted_mouse = mouse_pos;
            adjusted_mouse.x -= rep(viewport.offset_x);
            adjusted_mouse.y -= rep(viewport.offset_y);
            if (basic_aabb({ .pos = pos, .size = size }, adjusted_mouse))
            {
                data->ui_data.hover_scroll = true;
            }
            return;
        }

        if (should_begin_drag(*data) and data->ui_data.hover_scroll)
        {
            begin_drag(data.get());
        }

        if (dragging(*data))
        {
            mouse_move_drag(data.get(), mouse_pos, viewport);
        }
    }

    void ScrollBox::render(Render::SceneRenderer* renderer, const Render::RenderViewport& viewport)
    {
        renderer->set_shader(Render::VertShader::OneOneTransform);

        const auto& colors = Config::widget_colors();

        // Border rect for viewport.
        if (data->draw_border)
        {
            renderer->set_shader(Render::FragShader::BasicColor);
            Vec2f left{ 0.f, 0.f };
            Vec2f size{ rep(viewport.width) + 0.f, rep(viewport.height) + 0.f };
            renderer->strike_rect(left, size, 2.f, colors.scrollbar_track_outline);
            renderer->flush();
        }

        // Vert scroll bar.
        {
            renderer->set_shader(Render::FragShader::BasicColor);
            // Outline for track.
            Vec2f left{ rep(viewport.width) - Data::scrollbar_width, 0.f };
            Vec2f size{ Data::scrollbar_width, rep(viewport.height) + 0.f };
            renderer->strike_rect(left, size, 2.f, colors.scrollbar_track_outline);
            renderer->flush();

            // Scrollbar rect.
            auto [rect_pos, rect_size] = scrollbar_box(data.get(), viewport);
            const Vec4f scrollbar_color[] =
            {
                colors.scrollbar_inactive, // Neutral.
                colors.scrollbar_active    // Hovered.
            };
            renderer->solid_rect(rect_pos, rect_size, scrollbar_color[data->ui_data.hover_scroll]);
            renderer->flush();
        }
    }
} // namespace UI::Widgets