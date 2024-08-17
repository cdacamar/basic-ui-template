#include "basic-window.h"

#include <string>

#include "config.h"

namespace UI::Widgets
{
    namespace
    {
        enum class Resizing
        {
            None,
            Bottom,
            Left,
            Right,
            BottomLeft,
            BottomRight,
        };

        struct UIData
        {
            Vec2i last_mouse_down_start;
            Vec2i original_offset;
            Render::RenderViewport original_size;
            Resizing resizing = Resizing::None;
            bool dragging = false;
            bool mouse_down_on_title = false;
            bool hover_close_button = false;
            bool close_button_depressed = false;
        };
    } // namespace [anon]

    struct BasicWindow::Data
    {
        static constexpr int padding = 4;
        static constexpr float titlebar_height = 20;
        static constexpr Glyph::FontSize font_size = Glyph::FontSize{ 14 };

        std::string title;
        Vec2f button_size;
        UIData ui_data;
    };

    namespace
    {
        struct TitlebarRect
        {
            Vec2f pos;
            Vec2f size;
        };

        TitlebarRect titlebar_box(const Render::RenderViewport& viewport)
        {
            // Note: the viewport height is added to content size height because the viewport goes from y(0) to y(content.height).
            Vec2f left{ 0.f, rep(viewport.height) - BasicWindow::Data::titlebar_height };
            Vec2f size{ rep(viewport.width) + 0.f, BasicWindow::Data::titlebar_height };
            return { .pos = left, .size = size };
        }

        struct CloseButtonRect
        {
            Vec2f pos;
            Vec2f size;
        };

        CloseButtonRect close_button_box(const BasicWindow::Data& data, const Render::RenderViewport& viewport)
        {
            Vec2f left{ rep(viewport.width) - data.button_size.x, rep(viewport.height) - data.button_size.y };
            Vec2f size = data.button_size;
            return { .pos = left, .size = size };
        }

        bool dragging(const BasicWindow::Data& data)
        {
            return data.ui_data.dragging;
        }

        bool should_begin_drag(const BasicWindow::Data& data)
        {
            return not dragging(data);
        }

        void begin_drag(BasicWindow::Data* data, const Render::RenderViewport& viewport)
        {
            data->ui_data.dragging = true;
            data->ui_data.original_offset = { rep(viewport.offset_x), rep(viewport.offset_y) };
        }

        Vec2i mouse_move_drag(BasicWindow::Data* data, const Vec2i& mouse_pos)
        {
            auto offset = mouse_pos - data->ui_data.last_mouse_down_start;
            return data->ui_data.original_offset + offset;
        }

        void end_drag(BasicWindow::Data* data, const Vec2i&, const Render::RenderViewport&)
        {
            data->ui_data.dragging = false;
        }

        bool resizing(const BasicWindow::Data& data)
        {
            return data.ui_data.resizing != Resizing::None;
        }

        Resizing resizing_edge(const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
        {
            auto adjusted_mouse = adjusted_mouse_for_viewport(mouse_pos, viewport);
            if (adjusted_mouse.y <= BasicWindow::Data::padding)
            {
                if (adjusted_mouse.x <= BasicWindow::Data::padding)
                    return Resizing::BottomLeft;
                if ((rep(viewport.width) - adjusted_mouse.x) <= BasicWindow::Data::padding)
                    return Resizing::BottomRight;
                return Resizing::Bottom;
            }
            if (adjusted_mouse.x <= BasicWindow::Data::padding)
                return Resizing::Left;
            if ((rep(viewport.width) - adjusted_mouse.x) <= BasicWindow::Data::padding)
                return Resizing::Right;
            return Resizing::None;
        }

        bool should_begin_resize(const BasicWindow::Data& data, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
        {
            if (resizing(data))
                return false;
            // Identify which edge.
            auto edge = resizing_edge(mouse_pos, viewport);
            return edge != Resizing::None;
        }

        void begin_resize(BasicWindow::Data* data, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
        {
            data->ui_data.original_size = viewport;
            data->ui_data.resizing = resizing_edge(mouse_pos, viewport);
        }

        Render::RenderViewport mouse_move_resize(BasicWindow::Data* data, const Vec2i& mouse_pos)
        {
            auto offset = mouse_pos - data->ui_data.last_mouse_down_start;
            auto new_viewport = data->ui_data.original_size;
            switch (data->ui_data.resizing)
            {
            case Resizing::None:
                break;
            case Resizing::Bottom:
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.height = Height{ rep(new_viewport.height) - offset.y };
                new_viewport.offset_y = Render::ViewportOffsetY{ rep(new_viewport.offset_y) + offset.y };
                break;
            case Resizing::Left:
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.width = Width{ rep(new_viewport.width) - offset.x };
                new_viewport.offset_x = Render::ViewportOffsetX{ rep(new_viewport.offset_x) + offset.x };
                break;
            case Resizing::Right:
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.width = Width{ rep(new_viewport.width) + offset.x };
                break;
            case Resizing::BottomLeft:
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.height = Height{ rep(new_viewport.height) - offset.y };
                new_viewport.offset_y = Render::ViewportOffsetY{ rep(new_viewport.offset_y) + offset.y };
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.width = Width{ rep(new_viewport.width) - offset.x };
                new_viewport.offset_x = Render::ViewportOffsetX{ rep(new_viewport.offset_x) + offset.x };
                break;
            case Resizing::BottomRight:
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.height = Height{ rep(new_viewport.height) - offset.y };
                new_viewport.offset_y = Render::ViewportOffsetY{ rep(new_viewport.offset_y) + offset.y };
                // Adjust both the viewport height and offset by the same amount.
                new_viewport.width = Width{ rep(new_viewport.width) + offset.x };
                break;
            }
            return new_viewport;
        }

        void end_resize(BasicWindow::Data* data)
        {
            data->ui_data.resizing = Resizing::None;
        }

        WindowMouseArea area_from_resize(Resizing x)
        {
            switch (x)
            {
            case Resizing::None:
                return WindowMouseArea::None;
            case Resizing::Bottom:
                return WindowMouseArea::HorizBoarder;
            case Resizing::Left:
            case Resizing::Right:
                return WindowMouseArea::VertBoarder;
            case Resizing::BottomLeft:
                return WindowMouseArea::SWCorner;
            case Resizing::BottomRight:
                return WindowMouseArea::SECorner;
            }
            return WindowMouseArea::None;
        }

        // Assumes we're already in the client rect.
        bool process_mouse_move_resize(BasicWindow::Data*, WindowMouseResult* result, const UIState&, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
        {
            // Now we perform a more precise test to see if the mouse is on a horizontal boarder.
            auto edge = resizing_edge(mouse_pos, viewport);
            if (edge == Resizing::None)
                return false;
            result->area = area_from_resize(edge);
            return true;
        }

        // Assumes we're already in the client rect.
        bool process_mouse_down_resize(BasicWindow::Data* data, WindowMouseResult* result, const UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
        {
            // Now we perform a more precise test to see if the mouse is on a horizontal boarder.
            auto edge = resizing_edge(mouse_pos, viewport);
            if (edge == Resizing::None)
                return false;
            result->area = area_from_resize(edge);

            // Do not activate any resize behavior without mouse event.
            if (not implies(state.mouse, MouseState::LDown))
                return true;

            if (should_begin_resize(*data, mouse_pos, viewport))
            {
                begin_resize(data, mouse_pos, viewport);
            }

            result->resizing = true;

            if (resizing(*data))
            {
                result->resize_viewport = mouse_move_resize(data, mouse_pos);
            }
            return result;
        }
    } // namespace [anon]

    BasicWindow::BasicWindow():
        data{ new Data } { }

    BasicWindow::~BasicWindow() = default;

    // Setup.
    void BasicWindow::title(std::string_view s)
    {
        data->title = s;
    }

    // Queries for enclosed content.
    Render::RenderViewport BasicWindow::content_viewport(const Render::RenderViewport& viewport) const
    {
        // reduce the margins a bit.
        auto new_viewport = viewport;
        new_viewport.width = Width{ rep(viewport.width) - Data::padding * 2 };
        // Note: we remove 2 from padding here due to rendering 'up' in screen space.  We lose one pixel from the window boarder
        // on the bottom and then one more due to 'padding' being where we _start_ rednering.  However, we remove half pixel from the
        // top because the top of the box does not have this issue we end having this inconsistency.
        constexpr int render_start_offset = 2;
        new_viewport.height = Height{ rep(viewport.height) - Data::padding - Data::padding + render_start_offset - static_cast<int>(Data::titlebar_height) };
        new_viewport.offset_x = Render::ViewportOffsetX{ rep(viewport.offset_x) + Data::padding };
        new_viewport.offset_y = Render::ViewportOffsetY{ rep(viewport.offset_y) + Data::padding };
        return new_viewport;
    }

    // UI Interaction.
    WindowMouseResult BasicWindow::mouse_down(const UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
    {
        WindowMouseResult result{};
        // Reset flags.
        data->ui_data.close_button_depressed = false;
        if (not mouse_in_viewport(mouse_pos, viewport))
            return result;
        // Update UI data.
        data->ui_data.last_mouse_down_start = mouse_pos;
        auto titlebar_rect = titlebar_box(viewport);
        auto adjusted_mouse = adjusted_mouse_for_viewport(mouse_pos, viewport);
        if (not basic_aabb({ .pos = titlebar_rect.pos, .size = titlebar_rect.size }, adjusted_mouse))
        {
            if (process_mouse_down_resize(data.get(), &result, state, mouse_pos, viewport))
            {
                return result;
            }
            result.area = WindowMouseArea::Content;
            return result;
        }

        result.area = WindowMouseArea::Title;
        if (not implies(state.mouse, MouseState::LDown))
            return result;

        auto close_button_rect = close_button_box(*data, viewport);
        if (basic_aabb({ .pos = close_button_rect.pos, .size = close_button_rect.size }, adjusted_mouse))
        {
            // Don't start a drag on the close button.
            data->ui_data.close_button_depressed = true;
            return result;
        }

        if (should_begin_drag(*data))
        {
            begin_drag(data.get(), viewport);
        }

        result.dragging = true;

        if (dragging(*data))
        {
            result.move_offset = mouse_move_drag(data.get(), mouse_pos);
        }
        return result;
    }

    WindowMouseResult BasicWindow::mouse_up(const UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
    {
        WindowMouseResult result{};

        if (not implies(state.mouse, MouseState::LDown))
        {
            if (dragging(*data))
            {
                end_drag(data.get(), mouse_pos, viewport);
            }

            if (resizing(*data))
            {
                end_resize(data.get());
            }

            if (data->ui_data.close_button_depressed and data->ui_data.hover_close_button)
            {
                result.close = true;
            }
        }
        return result;
    }

    WindowMouseResult BasicWindow::mouse_move(const UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
    {
        // Clear state.
        data->ui_data.hover_close_button = false;

        WindowMouseResult result{};

        // If we're already dragging, continue this action.
        if (dragging(*data))
        {
            result.area = WindowMouseArea::Title;
            result.dragging = true;
            result.move_offset = mouse_move_drag(data.get(), mouse_pos);
            return result;
        }

        // Keep resizing.
        if (resizing(*data))
        {
            result.area = area_from_resize(data->ui_data.resizing);
            result.resizing = true;
            result.resize_viewport = mouse_move_resize(data.get(), mouse_pos);
            return result;
        }

        if (not mouse_in_viewport(mouse_pos, viewport))
            return result;
        auto titlebar_rect = titlebar_box(viewport);
        auto adjusted_mouse = adjusted_mouse_for_viewport(mouse_pos, viewport);
        if (not basic_aabb({ .pos = titlebar_rect.pos, .size = titlebar_rect.size }, adjusted_mouse))
        {
            if (process_mouse_move_resize(data.get(), &result, state, mouse_pos, viewport))
            {
                return result;
            }
            result.area = WindowMouseArea::Content;
            return result;
        }

        auto close_button_rect = close_button_box(*data, viewport);
        if (basic_aabb({ .pos = close_button_rect.pos, .size = close_button_rect.size }, adjusted_mouse))
        {
            data->ui_data.hover_close_button = true;
        }
        result.area = WindowMouseArea::Title;
        return result;
    }

    void BasicWindow::render(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const Render::RenderViewport& viewport)
    {
        renderer->set_shader(Render::VertShader::OneOneTransform);

        const auto& colors = Config::widget_colors();
        // Basic window rect.
        {
            renderer->set_shader(Render::FragShader::BasicColor);
            Vec2f left{ 0.f, 0.f };
            Vec2f size{ rep(viewport.width) + 0.f, rep(viewport.height) + 0.f };
            // First lets clear the rect.
            renderer->solid_rect(left, size, Config::system_colors().background);
            // Now strike it with the color we want.
            renderer->strike_rect(left, size, 2.f, colors.window_border);
            renderer->flush();
        }

        // Window title bar.
        {
            const float title_bar_start_y = rep(viewport.height) - Data::titlebar_height;
            data->button_size = Data::titlebar_height;

            renderer->set_shader(Render::FragShader::BasicColor);
            Vec2f left{ 0.f, title_bar_start_y };
            Vec2f size{ rep(viewport.width) + 0.f, Data::titlebar_height };
            renderer->solid_rect(left, size, colors.window_title_background);
            renderer->flush();

            // Render the close button hover if necessary.
            if (data->ui_data.hover_close_button)
            {
                // Reuse the shader from above.
                auto close_button_rect = close_button_box(*data, viewport);
                renderer->solid_rect(close_button_rect.pos, close_button_rect.size, colors.window_close_button_hover);
                renderer->flush();
            }

            auto font_ctx = atlas->render_font_context(Data::font_size);
            renderer->set_shader(Render::FragShader::Text);
            // Name.
            Vec2f pos{ Data::padding, 0.f };
            // Center the name.
            pos.y = title_bar_start_y + (Data::titlebar_height - rep(Data::font_size)) / 2.f;
            font_ctx.render_text(renderer, data->title, pos, colors.window_title_font_color);

            // 'X' button 'X'.
            auto [width, height] = font_ctx.glyph_size('X');
            pos.x = (rep(viewport.width) - data->button_size.x) + (data->button_size.x - width) / 2.f;
            pos.y = title_bar_start_y + (Data::titlebar_height + height) / 2.f;
            font_ctx.render_glyph_no_offsets(renderer, 'X', pos, colors.window_title_font_color);
            font_ctx.flush(renderer);
        }
    }
} // namespace UI::Widgets