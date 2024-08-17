#include "help.h"

#include <algorithm>
#include <vector>

#include "config.h"
#include "enum-utils.h"

namespace Help
{
    namespace
    {
        struct HelpEntry
        {
            std::string_view cmd;
            std::string_view desc;
        };

        constexpr HelpEntry commands[]
        {
            { .cmd = "F1 ",      .desc = " Show help" },
            { .cmd = "F5 ",      .desc = " Toggle show FPS" },
            { .cmd = "F6 ",      .desc = " Reload shaders" },
            { .cmd = "F9 ",      .desc = " Reload config (+CTRL to open config)" },
            { .cmd = "ESC ",     .desc = " Cancel command" },
        };

        constexpr HelpEntry shortcuts[]
        {
            { .cmd = "CTRL+w ",                          .desc = " Quit" },
        };

        struct HelpTable
        {
            const HelpEntry* longest_cmd;
            const HelpEntry* longest_desc;
        };

        template <int N>
        HelpTable compute_help_table(const HelpEntry (&table)[N])
        {
            size_t max_cmd = 0;
            size_t max_desc = 0;
            static_assert(N >= 1);
            for (size_t i = 1; i != N; ++i)
            {
                if (table[max_cmd].cmd.size() < table[i].cmd.size())
                {
                    max_cmd = i;
                }

                if (table[max_desc].desc.size() < table[i].desc.size())
                {
                    max_desc = i;
                }
            }
            return { .longest_cmd = &table[max_cmd], .longest_desc = &table[max_desc] };
        }
    } // namespace [anon]

    struct Help::Data
    {
        HelpTable commands_table_meta = compute_help_table(commands);
        HelpTable shortcuts_table_meta = compute_help_table(shortcuts);

        static constexpr auto font_size = Glyph::FontSize{ 18 };
        static constexpr float outline_thickness = 2.f;
    };

    namespace
    {
        struct BoundingBox
        {
            Vec2f box;
            Vec2f column_1;
            Vec2f column_2;
        };

        template <int N>
        BoundingBox bounding_box_for(const HelpTable& meta, const HelpEntry (&)[N], Glyph::RenderFontContext* font_ctx)
        {
            // First, identify the size of the longest command based on the font size so we can render all commands to that rect
            // then move to the descriptions of each command.
            auto measure = font_ctx->measure_text(meta.longest_cmd->cmd);
            Vec2f first_pos = measure;
            first_pos.y = static_cast<float>(rep(Help::Data::font_size) * N);

            // Then we have the second position (the descriptions).
            measure = font_ctx->measure_text(meta.longest_desc->desc);
            Vec2f second_pos = measure;
            second_pos.y = first_pos.y;

            // Final bounding box.
            Vec2f bounding_box = first_pos;
            bounding_box.x += second_pos.x;

            return { .box = bounding_box, .column_1 = first_pos, .column_2 = second_pos };
        }
    } // namespace [anon]

    Help::Help():
        data{ new Data } { }

    Help::~Help() = default;

    void Help::render(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const ScreenDimensions& screen)
    {
        // Blur the background for some flare.
        {
            // The background is already rendered to the default framebuffer, so we can just take that,
            // blur it, and render it back to the default framebuffer.
            renderer->set_shader(Render::VertShader::NoTransform);
            Render::Effects::blur_background({ .src = Render::Framebuffer::Default, .dest = Render::Framebuffer::Default },
                                                renderer,
                                                Render::RenderViewport::basic(screen),
                                                screen);
            // Default the blending mode.
            renderer->apply_blending_mode(Render::BlendingMode::Default);
        }

        const auto& colors = Config::system_colors();

        // Setup a background so it is easier to see the help.
        Vec4f bg_color = colors.background;
        bg_color.a = 0.6f;
        Render::draw_background(renderer, screen, bg_color);
        auto font_ctx = atlas->render_font_context(Data::font_size);

        // To layout everything properly, we first need to compute the size of all the text tables.
        renderer->set_shader(Render::VertShader::OneOneTransform);

        auto [commands_box, commands_col1, commands_col2] = bounding_box_for(data->commands_table_meta, commands, &font_ctx);
        auto [sc_box, sc_col1, sc_col2] = bounding_box_for(data->shortcuts_table_meta, shortcuts, &font_ctx);

        Vec2f all_containers{ commands_box.x + sc_box.x, std::max(commands_box.y, sc_box.y) };
        // Add some padding.
        constexpr float padding = rep(Data::font_size) / 8.f;
        all_containers.x += padding;

        // Mount to the center.
        Vec2f box_pos;
        box_pos.x = ((rep(screen.width) - all_containers.x) / 2.f);
        box_pos.y = ((rep(screen.height) + all_containers.y) / 2.f);

        Vec2f pos = box_pos;
        renderer->set_shader(Render::FragShader::Text);
        const Vec4f color = colors.default_font_color;
        // Commands.
        // Render "Commands" in center.
        auto title_pos = pos;
        title_pos.x += (commands_box.x - font_ctx.measure_text("Commands").x) / 2.f;
        font_ctx.render_text(renderer, "Commands", title_pos, color);
        pos.y -= rep(Data::font_size);

        for (auto& cmd : commands)
        {
            font_ctx.render_text(renderer, cmd.cmd, pos, color);
            pos.x = box_pos.x + commands_col1.x;
            font_ctx.render_text(renderer, cmd.desc, pos, color);
            pos.y -= rep(Data::font_size);
            pos.x = box_pos.x;
        }

        // Now the shortcuts.
        pos = box_pos;
        const float box_offset = commands_box.x + padding;
        pos.x += box_offset;

        // Render "Shortcuts" in center.
        title_pos = pos;
        title_pos.x += (sc_box.x - font_ctx.measure_text("Shortcuts").x) / 2.f;
        font_ctx.render_text(renderer, "Shortcuts", title_pos, color);
        pos.y -= rep(Data::font_size);

        for (auto& sc : shortcuts)
        {
            font_ctx.render_text(renderer, sc.cmd, pos, color);
            pos.x = box_pos.x + box_offset + sc_col1.x;
            font_ctx.render_text(renderer, sc.desc, pos, color);
            pos.y -= rep(Data::font_size);
            pos.x = box_pos.x + box_offset;
        }
        font_ctx.flush(renderer);

        // Now we can put a small box around each one.
        // Commands box.
        pos = box_pos;
        pos.x -= padding;
        pos.y -= rep(Data::font_size) * std::size(commands);
        auto size = commands_box + padding;
        pos.y -= padding * 2.f;
        renderer->set_shader(Render::FragShader::BasicColor);
        renderer->strike_rect(pos, size, Data::outline_thickness, color);

        // The separators.
        // Commands separator.
        pos = box_pos;
        pos.x = box_pos.x + commands_col1.x;
        pos.y -= rep(Data::font_size) * std::size(commands);
        pos.y -= padding * 2.f;
        size = commands_box + padding;
        size.x = Data::outline_thickness;
        renderer->solid_rect(pos, size, color);

        // Shortcuts separator.
        pos = box_pos;
        pos.x = box_pos.x + box_offset + sc_col1.x;
        pos.y -= rep(Data::font_size) * std::size(shortcuts);
        pos.y -= padding * 2.f;
        size = sc_box + padding;
        size.x = Data::outline_thickness;
        renderer->solid_rect(pos, size, color);

        // Shortcuts box.
        pos = box_pos;
        pos.x += box_offset;
        pos.x -= padding;
        pos.y -= rep(Data::font_size) * std::size(shortcuts);
        size = sc_box + padding;
        pos.y -= padding * 2.f;
        renderer->strike_rect(pos, size, Data::outline_thickness, color);

        renderer->flush();
    }
} // namespace Help