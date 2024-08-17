#include "choice.h"

#include <string>
#include <vector>

#include "constants.h"
#include "enum-utils.h"

namespace Choice
{
    namespace
    {
        using ChoiceContainer = std::vector<std::string>;

        enum class Selection : size_t { };
    } // namespace [anon]

    struct Chooser::Data
    {
        ChoiceContainer choices;
        std::string reason;
        Selection selection{};
        static constexpr float cursor_offset = 0.13f;

        static constexpr auto title_font_size = Glyph::FontSize{ 32 };
        static constexpr auto font_size = Glyph::FontSize{ 64 };
    };

    Chooser::Chooser():
        data{ new Data } { }

    Chooser::~Chooser() = default;

    void Chooser::choice_count(size_t n)
    {
        data->choices.clear();
        data->choices.reserve(n);
        data->selection = {};
    }

    void Chooser::add_choice(std::string_view choice)
    {
        data->choices.emplace_back(choice);
    }

    void Chooser::reason(std::string_view s)
    {
        data->reason = s;
    }

    size_t Chooser::selection() const
    {
        return rep(data->selection);
    }

    std::string_view Chooser::selection_string() const
    {
        return data->choices[rep(data->selection)];
    }

    void Chooser::up()
    {
        if (rep(data->selection) > 0)
        {
            data->selection = retract(data->selection);
        }
    }

    void Chooser::down()
    {
        if (rep(extend(data->selection)) < data->choices.size())
        {
            data->selection = extend(data->selection);
        }
    }

    void Chooser::top()
    {
        data->selection = {};
    }

    void Chooser::bottom()
    {
        data->selection = Selection{ data->choices.size() - 1 };
    }

    void Chooser::render(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const ScreenDimensions& screen)
    {
        // Setup a background so it is easier to see the choices.
        constexpr Vec4f bg_color = Vec4f(0.f, 0.f, 0.f, 0.85f);
        Render::draw_background(renderer, screen, bg_color);
        auto font_ctx = atlas->render_font_context(Data::font_size);

        // We don't want to transform any text at the top.
        renderer->set_shader(Render::VertShader::OneOneTransform);
        // Render the choice description at the top.
        {
            auto title_font_ctx = atlas->render_font_context(Data::title_font_size);
            renderer->set_shader(Render::FragShader::Text);
            Vec2f pos = Vec2f(10.f, rep(screen.height) - rep(Data::title_font_size) - 10.f);
            constexpr Vec4f color = hex_to_vec4f(0xFFFFFFFF);
            title_font_ctx.render_text(renderer, data->reason, pos, color);
            title_font_ctx.flush(renderer);
        }

        // Similar to the editor, we want camera transforms.
        renderer->set_shader(Render::VertShader::CameraTransform);

        // Render the selection rect.
        Vec2f selection_pos;
        {
            renderer->set_shader(Render::FragShader::BasicColor);
            const auto& selection = data->choices[rep(data->selection)];
            selection_pos = Vec2f(0.f, -(rep(data->selection) + Data::cursor_offset) * rep(Data::font_size));
            auto size = font_ctx.measure_text(selection);
            size.y = static_cast<float>(rep(Data::font_size));
            constexpr auto color = hex_to_vec4f(0x7E8081AA);
            renderer->solid_rect(selection_pos, size, color);
            renderer->flush();
        }

        // Render entries.
        float max_line_len = 0.0001f;
        {
            renderer->set_shader(Render::FragShader::Text);
            Vec2f line_pos{};
            for (auto& entry : data->choices)
            {
                constexpr auto color = hex_to_vec4f(0xFFFFFFFF);
                auto pos = font_ctx.render_text(renderer, entry, line_pos, color);
                line_pos.y -= static_cast<float>(rep(Data::font_size));
                max_line_len = std::max(pos.x, max_line_len);
            }
            font_ctx.flush(renderer);
        }

        // Camera transform.
        {
            const float total_line_dist = static_cast<float>(data->choices.size()) * rep(Data::font_size);
            // We want the camera to zoom out as the user adds new lines otherwise lines earlier
            // may be harder to see.
            max_line_len += total_line_dist;
            max_line_len = std::min(max_line_len, 1000.f);

            const float zoom_factor_x = rep(screen.width) / 3.f;

            float target_scale_x = zoom_factor_x / (max_line_len * 0.75f);

            auto camera = renderer->camera();
            camera = Render::cursor_camera_transform(camera, selection_pos, target_scale_x, zoom_factor_x, renderer->delta_time());
            renderer->camera(camera);
        }
    }
} // Choice