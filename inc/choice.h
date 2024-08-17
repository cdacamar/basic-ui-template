#pragma once

#include <memory>
#include <string_view>

#include "glyph-cache.h"
#include "renderer.h"
#include "types.h"

namespace Choice
{
    class Chooser
    {
    public:
        struct Data;

        Chooser();
        ~Chooser();

        // Initialization.
        void choice_count(size_t n);
        void add_choice(std::string_view choice);
        void reason(std::string_view s);
        // Interaction.
        size_t selection() const;
        std::string_view selection_string() const;

        // Navigation.
        void up();
        void down();
        void top();
        void bottom();

        // Rendering.
        void render(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const ScreenDimensions& screen);
    private:
        std::unique_ptr<Data> data;
    };
} // namespace Choice