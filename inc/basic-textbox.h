#include <memory>
#include <string_view>

#include "glyph-cache.h"
#include "renderer.h"
#include "vec.h"

namespace UI::Widgets
{
    class BasicTextbox
    {
    public:
        struct Data;

        BasicTextbox();
        ~BasicTextbox();

        // Interaction.
        void text(std::string_view text);
        void offset(const Vec2f& offset);
        void font_size(Glyph::FontSize size);

        // Queries.
        Vec2f content_size(Glyph::Atlas* atlas) const;

        void render(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const Render::RenderViewport& viewport);
    private:
        std::unique_ptr<Data> data;
    };
} // namespace UI::Widgets