#pragma once

#include <memory>
#include <string_view>

#include "glyph-cache.h"
#include "renderer.h"
#include "types.h"

namespace Help
{
    class Help
    {
    public:
        struct Data;

        Help();
        ~Help();

        void render(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const ScreenDimensions& screen);
    private:
        std::unique_ptr<Data> data;
    };
} // namespace Help