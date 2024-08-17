#pragma once

#include <memory>

#include "glyph-cache.h"
#include "renderer.h"
#include "types.h"
#include "ui-common.h"
#include "vec.h"

namespace Examples
{
    class Intro
    {
    public:
        void render(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const ScreenDimensions& screen);
    };

    class DragNSnap
    {
    public:
        struct Data;

        DragNSnap();
        ~DragNSnap();

        // UI Interaction.
        void mouse_down(const UI::UIState& state, const Vec2i& mouse_pos);
        void mouse_up(const UI::UIState& state, const Vec2i& mouse_pos);
        void mouse_move(const UI::UIState& state, const Vec2i& mouse_pos, const Render::RenderViewport& viewport);

        void render(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const Render::RenderViewport& viewport);
    private:
        std::unique_ptr<Data> data;
    };
} // namespace Examples