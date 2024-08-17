#pragma once

#include <memory>
#include <string_view>

#include "glyph-cache.h"
#include "renderer.h"
#include "types.h"

namespace Feed
{
    class MessageFeed
    {
    public:
        struct Data;

        MessageFeed();
        ~MessageFeed();

        void queue_info(std::string_view message);
        void queue_error(std::string_view error);
        void queue_warning(std::string_view warning);
        void render_queue(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const ScreenDimensions& screen);
    private:
        void reap();

        std::unique_ptr<Data> data;
    };
} // namespace Feed