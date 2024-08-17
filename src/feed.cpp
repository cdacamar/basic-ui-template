#include "feed.h"

#include <string>
#include <vector>

#include <SDL2/SDL_timer.h>

#include "config.h"
#include "enum-utils.h"
#include "util.h"
#include "vec.h"

namespace Feed
{
    namespace
    {
        struct MessageData
        {
            std::string message;
            Uint32 start;
            Vec4f color;

            // 10 seconds.
            static constexpr int message_lifetime = 5'000;
        };

        using Messages = std::vector<MessageData>;

        Vec4f decay_message_color(const MessageData& data, Uint32 time)
        {
            auto new_color = data.color;
            // We want the messages to live for a prescribed amount of time.
            const auto final_time = data.start + MessageData::message_lifetime;
            // Your time has passed...
            if (final_time < time)
            {
                new_color.a = 0.f;
                return new_color;
            }
            const auto dt = final_time - time;
            float percent = 1.f - static_cast<float>(dt) / MessageData::message_lifetime;
            percent = std::min(1.f, percent);
            new_color.a = lerp(data.color.a, 0.f, percent);
            return new_color;
        }
    } // namespace [anon]

    struct MessageFeed::Data
    {
        Messages messages;
    };

    MessageFeed::MessageFeed():
        data{ new Data } { }

    MessageFeed::~MessageFeed() = default;

    void MessageFeed::queue_info(std::string_view message)
    {
        // Reap before we push_back to possibly avoid the allocation.
        reap();
        data->messages.push_back({ .message = std::string{ message },
                                   .start = SDL_GetTicks(),
                                   .color = Config::feed_colors().info });
    }

    void MessageFeed::queue_error(std::string_view error)
    {
        // Reap before we push_back to possibly avoid the allocation.
        reap();
        data->messages.push_back({ .message = std::string{ error },
                                   .start = SDL_GetTicks(),
                                   .color = Config::feed_colors().error });
    }

    void MessageFeed::queue_warning(std::string_view warning)
    {
        // Reap before we push_back to possibly avoid the allocation.
        reap();
        data->messages.push_back({ .message = std::string{ warning },
                                   .start = SDL_GetTicks(),
                                   .color = Config::feed_colors().warning });
    }

    void MessageFeed::render_queue(Render::SceneRenderer* renderer, Glyph::Atlas* atlas, const ScreenDimensions&)
    {
        // DO NOT reap() in the render loop!  This is performance-sensitive.

        // Set the appropriate vertex and fragment shaders.
        // We do not want camera transforms changing the position of this text.
        renderer->set_shader(Render::VertShader::OneOneTransform);

        const auto& state = Config::feed_state();
        const auto& editor_colors = Config::system_colors();

        Glyph::FontSize font_size{ state.feed_font_size };

        // Get the font context for the rendering loop.
        auto font_ctx = atlas->render_font_context(font_size);

        const auto ticks = SDL_GetTicks();

        constexpr float render_offset = 20.f;

        // Render each message.
        Vec2f pos = Vec2f(render_offset, render_offset);

        auto first = rbegin(data->messages);
        auto last = rend(data->messages);
        // Render the backgrounds for better readability when lots of text is
        // present in an editor view.
        renderer->set_shader(Render::FragShader::BasicColor);
        auto bg_pos = pos;
        // This helps the final rect to be positioned correctly (10% of the font size should wrap the
        // entire message).
        bg_pos.y = render_offset - rep(font_size) * 0.1f;
        Vec2f bg_size { 0.f, static_cast<float>(font_size) };
        for (auto itr = first; itr != last; ++itr)
        {
            auto& msg = *itr;
            // Compute the background width.
            bg_size.x = font_ctx.measure_text(msg.message).x;
            // Inherit the background from the editor for a nice fade effect.
            auto color = editor_colors.background;
            color.a = decay_message_color(msg, ticks).a;
            renderer->solid_rect(bg_pos, bg_size, color);
            bg_pos.y += rep(font_size);
        }
        renderer->flush();

        // Render the text.
        renderer->set_shader(Render::FragShader::Text);
        for (; first != last; ++first)
        {
            auto& msg = *first;
            auto color = decay_message_color(msg, ticks);
            font_ctx.render_text(renderer, msg.message, pos, color);
            pos.y += rep(font_size);
        }
        font_ctx.flush(renderer);
    }

    void MessageFeed::reap()
    {
        const auto now = SDL_GetTicks();
        // Since the message feed queues messages in order, all we need to do is find the first one which is dead starting from the end.
        auto first_dead = std::find_if(rbegin(data->messages),
                                        rend(data->messages),
                                        [&](const auto& msg)
                                        {
                                            return now - msg.start > MessageData::message_lifetime;
                                        });
        auto first_real = first_dead.base();
        // No messages to reap.
        if (first_real == end(data->messages))
            return;
        data->messages.erase(begin(data->messages), first_real);
    }
} // namespace Feed