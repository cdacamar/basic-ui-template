#pragma once

#include "enum-utils.h"
#include "types.h"

namespace Constants
{
    constexpr ScreenDimensions screen{ Width{ 1024 }, Height{ 768 } };

    constexpr FPS target_fps{ 240 };

    constexpr float max_camera_zoom = 3.f;

    // Note: This is a compensation factor used to properly scale images and objects drawn to the screen.  Since we draw
    // images according to the dimensions provided by callers to draw() but the OpenGL coordinate system is in entire
    // screen dimensions (-1, 1) which is really (resolution * 2) to get to '1', we need to rescale by that same factor
    // in the vertex shader to get an expected image/shape size on screen when going through the transform shader.  Note
    // that the no-transform shader does not use this.
    constexpr float shader_scale_factor = 2.f;
} // namespace Constants