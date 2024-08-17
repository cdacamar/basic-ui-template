#pragma once

#include <stdint.h>

#include "renderer.h"
#include "vec.h"

namespace UI
{
    enum class KeyMods : uint8_t
    {
        None  = 0,
        Shift = 1u << 0,
        Alt   = 1u << 1,
        Ctrl  = 1u << 2,
    };

    enum class MouseState : uint8_t
    {
        None      = 0,
        LDown     = 1u << 0,
        RDown     = 1u << 1,
        Middle    = 1u << 2,
        AnyDown   = LDown | RDown | Middle
    };

    enum class SpecialModes : uint8_t
    {
        None             = 0,
        ShowGlyphs       = 1u << 0,
        SuspendRendering = 1u << 1,
        ShowFPS          = 1u << 2,
    };

    struct UIState
    {
        KeyMods mods{};
        MouseState mouse{};
        SpecialModes special{};
    };

    enum class AdditionalKeyInputs : uint32_t
    {
        None  = 0,
        Shift = 1u << 0,
        Alt   = 1u << 1,
    };

    struct AABBData
    {
        Vec2f pos;
        Vec2f size;
    };

    constexpr bool basic_aabb(const AABBData& box, const Vec2i& point)
    {
        return box.pos.x + box.size.x > point.x
            and box.pos.x <= point.x
            and box.pos.y + box.size.y > point.y
            and box.pos.y <= point.y;
    }

    constexpr Vec2i adjusted_mouse_for_viewport(const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
    {
        auto adjusted_mouse = mouse_pos;
        adjusted_mouse.x -= rep(viewport.offset_x);
        adjusted_mouse.y -= rep(viewport.offset_y);
        return adjusted_mouse;
    }

    constexpr bool mouse_in_viewport(const Vec2i& mouse_pos, const Render::RenderViewport& viewport)
    {
        auto adjusted_mouse = adjusted_mouse_for_viewport(mouse_pos, viewport);
        Vec2f pos{ 0.f, 0.f };
        Vec2f size{ rep(viewport.width) + 0.f, rep(viewport.height) + 0.f };
        return basic_aabb({ .pos = pos, .size = size }, adjusted_mouse);
    }
} // namespace UI