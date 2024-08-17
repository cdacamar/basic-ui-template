#include "basic-textbox.h"

#include <string>
#include <vector>

#include "config.h"

namespace UI::Widgets
{
    using namespace Text;

    namespace
    {
        using LineStarts = std::vector<CharOffset>;

        void populate_line_starts(std::string_view text, LineStarts* line_starts)
        {
            line_starts->clear();
            line_starts->push_back(CharOffset{});
            for (size_t i = 0; i != text.size(); ++i)
            {
                if (text[i] == '\n')
                {
                    line_starts->push_back(CharOffset{ i + 1 });
                }
            }
        }
    } // namespace [anon]

    struct BasicTextbox::Data
    {
        std::string text;
        LineStarts line_starts;
        Vec2f offset;
        Glyph::FontSize font_size = Glyph::FontSize{ 18 };
    };

    namespace
    {
        enum class Line : size_t { };
        Line text_start_for_visual_offset(BasicTextbox::Data* data, Glyph::RenderFontContext* font_ctx)
        {
            // Only consider vertical offset for now.
            const auto line_height = font_ctx->current_font_line_height();
            const auto offset = data->offset.y;
            return Line{ static_cast<size_t>(offset / line_height) };
        }

        struct LineRange
        {
            CharOffset first;
            CharOffset last;
        };

        LineRange line_range(BasicTextbox::Data* data, Line line)
        {
            if (rep(line) >= data->line_starts.size())
                return { CharOffset{}, CharOffset{ data->text.size() } };
            auto start = data->line_starts[rep(line)];
            auto next_line = extend(line);
            if (rep(next_line) >= data->line_starts.size())
                return { start, CharOffset{ data->text.size() } };
            auto end = data->line_starts[rep(next_line)];
            return { start, retract(end) };
        }

        std::string_view line_text(BasicTextbox::Data* data, Line line)
        {
            auto [first, last] = line_range(data, line);
            return std::string_view{ &data->text[rep(first)], rep(last) - rep(first) };
        }
    } // namespace [anon]

    BasicTextbox::BasicTextbox():
        data{ new Data } { }

    BasicTextbox::~BasicTextbox() = default;

    void BasicTextbox::text(std::string_view text)
    {
        data->text = text;
        populate_line_starts(text, &data->line_starts);
    }

    void BasicTextbox::offset(const Vec2f& offset)
    {
        data->offset = offset;
    }

    void BasicTextbox::font_size(Glyph::FontSize size)
    {
        data->font_size = size;
    }

    Vec2f BasicTextbox::content_size(Glyph::Atlas* atlas) const
    {
        Vec2f size;
        auto font_ctx = atlas->render_font_context(data->font_size);
        const auto line_height = font_ctx.current_font_line_height();
        // Measure each line of text.
        auto first = begin(data->text);
        auto last = end(data->text);
        auto current_line = first;
        for (; first != last; ++first)
        {
            if (*first == '\n')
            {
                // Measure the line.
                auto line = std::string_view{ &*current_line, static_cast<size_t>(first - current_line) };
                auto measure = font_ctx.measure_text(line);
                size.x += measure.x;
                size.y += line_height;
                // Skip the '\n'.
                current_line = next(first);
            }
        }

        if (current_line != last)
        {
            // Measure the last line.
            auto line = std::string_view{ &*current_line, static_cast<size_t>(last - current_line) };
            auto measure = font_ctx.measure_text(line);
            size.x += measure.x;
            size.y += line_height;
        }

        // Remove an extra line to always make the last line visible.
        if (size.y >= line_height)
        {
            size.y -= line_height;
        }

        return size;
    }

    void BasicTextbox::render(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const Render::RenderViewport& viewport)
    {
        // Find the first line to render.
        auto font_ctx = atlas->render_font_context(data->font_size);
        Line line = text_start_for_visual_offset(data.get(), &font_ctx);
        // Nothing to render.
        if (rep(line) >= data->line_starts.size())
            return;
        auto line_height = font_ctx.current_font_line_height();
        auto start_y = rep(viewport.height) + fmodf(data->offset.y, static_cast<float>(line_height)) - line_height;
        Vec2f pos{ 0.f, start_y };
        auto last = data->line_starts.size();
        renderer->set_shader(Render::VertShader::OneOneTransform);
        renderer->set_shader(Render::FragShader::Text);
        for (; rep(line) < last; line = extend(line))
        {
            auto txt = line_text(data.get(), line);
            font_ctx.render_text(renderer, txt, pos, Config::system_colors().default_font_color);
            pos.y -= line_height;

            if (pos.y > rep(viewport.height))
                break;
        }
        font_ctx.flush(renderer);
    }
} // namespace UI::Widgets