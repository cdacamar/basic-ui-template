#pragma once

#include "types.h"

namespace Feed
{
    class MessageFeed;
} // namespace Feed

namespace Theme
{
    void init(Feed::MessageFeed* feed);
    void apply_boarder_color(OpaqueWindow window, Feed::MessageFeed* feed);
} // namespace Theme