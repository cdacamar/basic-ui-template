#pragma once

#include <string_view>

#include "renderer.h"
#include "types.h"

namespace Feed
{
    class MessageFeed;
} // namespace Feed

namespace SVG
{
    struct LoadSVGResult
    {
        Render::BasicTexture tex;
        ScreenDimensions size;
    };

    LoadSVGResult load_svg(std::string_view path, Feed::MessageFeed* feed);
} // namespace SVG