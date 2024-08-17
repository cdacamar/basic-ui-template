#pragma once

#include <memory>
#include <string_view>

#include "renderer.h"
#include "types.h"
#include "utf-8.h"
#include "vec.h"

namespace Feed
{
    class MessageFeed;
} // namespace Feed

namespace Glyph
{
    struct CachedFont;
    class Atlas;

    struct CustomContextColors
    {
        Vec4f whitespace;
        Vec4f carriage_return;
    };

    class RenderFontContext
    {
    public:
        // Returns a new position to start rendering from.
        Vec2f render_text(Render::SceneRenderer* renderer,
                            std::string_view text,
                            const Vec2f& pos,
                            const Vec4f& color);
        Vec2f render_glyph(Render::SceneRenderer* renderer,
                            UTF8::Codepoint cp,
                            const Vec2f& pos,
                            const Vec4f& color);
        // Similar to the above, but does not take bitmap top or left into account.
        Vec2f render_glyph_no_offsets(Render::SceneRenderer* renderer,
                            UTF8::Codepoint cp,
                            const Vec2f& pos,
                            const Vec4f& color);
        Vec2f render_scaled_text(Render::SceneRenderer* renderer,
                            std::string_view text,
                            float scalar,
                            const Vec2f& pos,
                            const Vec4f& color);

        // Flushes render queue for text.
        void flush(Render::SceneRenderer* renderer);

        // Measurement functions.
        Vec2f measure_text(std::string_view text);
        Vec2f measure_scaled_text(std::string_view text, float scalar);
        Vec2f glyph_size(UTF8::Codepoint cp);
        size_t glyph_count_to_point(std::string_view text, float x_point);
        int current_font_size();
        int current_font_line_height();

        // Configuration.
        void tabstop(Tabstop ts);
        void whitespace_color(const Vec4f& color);
        void carriage_return_color(const Vec4f& color);
        void render_whitespace(bool b);
    private:
        friend Atlas;
        RenderFontContext(Atlas* atlas, CachedFont* font);

        Atlas* atlas;
        CachedFont* font;
        Tabstop tabs;
        CustomContextColors colors;
        bool render_ws;
    };

    class Atlas
    {
    public:
        struct Data;

        Atlas();
        ~Atlas();

        // Only used for library initialization.
        bool init(std::string_view font_path);
        bool populate_atlas();

        // App interaction.
        void try_load_font_face(std::string_view path, Feed::MessageFeed* feed);
        std::string_view font_family() const;

        // Acquire font renderer.
        RenderFontContext render_font_context(FontSize size);

        // For when the renderer updates.
        void bind_primary_texture();

    private:
        friend RenderFontContext;
        std::unique_ptr<Data> data;
    };
} // namespace Glyph