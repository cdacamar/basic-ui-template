#pragma once

#include <memory>

#include "renderer.h"
#include "ui-common.h"
#include "vec.h"

namespace UI::Widgets
{
    struct ScrollBox
    {
    public:
        struct Data;

        ScrollBox();
        ~ScrollBox();

        // Setup.
        void content_size(const Vec2f& size);
        void draw_border(bool b);

        // Queries for enclosed content.
        Vec2f position() const;
        Render::RenderViewport content_viewport(const Render::RenderViewport& viewport) const;

        // UI Interaction.
        void scroll_up(float amount, const Vec2i& mouse_pos, const Render::RenderViewport& viewport);
        void scroll_down(float amount, const Vec2i& mouse_pos, const Render::RenderViewport& viewport);
        void mouse_down(const UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport);
        void mouse_up(const UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport);
        void mouse_move(const UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport);

        void render(Render::SceneRenderer* renderer, const Render::RenderViewport& viewport);
    private:
        std::unique_ptr<Data> data;
    };
} // namespace UI::Widgets