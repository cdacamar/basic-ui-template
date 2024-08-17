#pragma once

#include <memory>
#include <string_view>

#include "glyph-cache.h"
#include "renderer.h"
#include "types.h"
#include "ui-common.h"

namespace UI::Widgets
{
    enum class WindowMouseArea
    {
        None,
        Content,
        Title,
        VertBoarder,
        HorizBoarder,
        SECorner,    // South East corner.
        SWCorner,    // South West corner.
    };

    struct WindowMouseResult
    {
        WindowMouseArea area = WindowMouseArea::None;
        Vec2i move_offset;
        Render::RenderViewport resize_viewport;
        bool close = false;
        bool dragging = false;
        bool resizing;
    };

    class BasicWindow
    {
    public:
        struct Data;

        BasicWindow();
        ~BasicWindow();

        // Setup.
        void title(std::string_view s);

        // Queries for enclosed content.
        Render::RenderViewport content_viewport(const Render::RenderViewport& viewport) const;

        // UI Interaction.
        WindowMouseResult mouse_down(const UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport);
        WindowMouseResult mouse_up(const UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport);
        WindowMouseResult mouse_move(const UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport);

        void render(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const Render::RenderViewport& viewport);
    private:
        std::unique_ptr<Data> data;
    };
} // namespace UI::Widgets