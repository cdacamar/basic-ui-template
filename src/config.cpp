#include "config.h"

#include <cassert>

#include <algorithm>
#include <format>
#include <sstream>

#include <toml.hpp>

#include "feed.h"
#include "util.h"

namespace Config
{
    namespace
    {
        // Helpful color functions.
        // Ignore alpha.
        float max_color(Vec4f color)
        {
            return std::max(color.x, std::max(color.y, color.z));
        }

        float min_color(Vec4f color)
        {
            return std::min(color.x, std::min(color.y, color.z));
        }

        Vec4f rgb_to_hsv(Vec4f color)
        {
            // see https://en.wikipedia.org/wiki/HSL_and_HSV#Formal_derivation
            // Our color is already in the range [0, 1] so all we need are the
            // min and max values.
            float max = max_color(color);
            float min = min_color(color);
            float d = max - min;
            float v = max;

            float sat = 0.f;
            float hue = 0.f;
            if (max != 0.f)
            {
                sat = d / max;
            }

            if (max != min)
            {
                if (max == color.x)
                {
                    hue = (color.y - color.z) / d;
                    if (color.y < color.z)
                    {
                        hue += 6.f;
                    }
                }
                else if (max == color.y)
                {
                    hue = (color.z - color.x) / d + 2.f;
                }
                else if (max == color.z)
                {
                    hue = (color.x - color.y) / d + 4.f;
                }
                hue /= 6.f;
            }
            return { hue, sat, v, color.a };
        }

        Vec4f hsv_to_rgb(Vec4f hsv)
        {
            float h = hsv.x;
            float s = hsv.y;
            float v = hsv.z;

            int i = static_cast<int>(std::floor(h * 6.f));

            float f = h * 6.f - i;
            float p = v * (1.f - s);
            float q = v * (1.f - f * s);
            float t = v * (1.f - (1.f - f) * s);

            int rem = i % 6;

            Vec4f result = hsv;

            switch (rem)
            {
            case 0:
                result.x = v;
                result.y = t;
                result.z = p;
                break;
            case 1:
                result.x = q;
                result.y = v;
                result.z = p;
                break;
            case 2:
                result.x = p;
                result.y = v;
                result.z = t;
                break;
            case 3:
                result.x = p;
                result.y = q;
                result.z = v;
                break;
            case 4:
                result.x = t;
                result.y = p;
                result.z = v;
                break;
            case 5:
                result.x = v;
                result.y = p;
                result.z = q;
                break;
            default:
                // We mod by 6 so no other case is possible.
                assert(false);
            }
            return result;
        }

        // The computes the brightness based on the W3C formula.
        float weighted_w3c(Vec4f color)
        {
            // Source: https://mixable.blog/black-or-white-text-on-a-colour-background/
            float bright_r = color.x * 255.f * 0.299f;
            float bright_g = color.y * 255.f * 0.587f;
            float bright_b = color.z * 255.f * 0.114f;
            return bright_r + bright_g + bright_b;
        }

        // Sourced from: https://mixable.blog/adjust-text-color-to-be-readable-on-light-and-dark-backgrounds-of-user-interfaces/
        Vec4f readable_color_for_any_bg(Vec4f color)
        {
            auto hsv = rgb_to_hsv(color);

            constexpr float step = 0.01f;
            // Normally we're supposed to use 127.f but I have found that, since we, by default, want to be in the dark-mode
            // spectrum, it makes more sense to tune the colors biased towards light backgrounds when 'light_mode' is active,
            // meaning we're going to tend towards darkening all colors.
            //constexpr float bright_cap = 127.f;
            constexpr float bright_cap = 115.f;

            float brightness = weighted_w3c(color);
            if (brightness < bright_cap)
            {
                while (brightness < bright_cap and hsv.z >= 0.f and hsv.z <= 1.f)
                {
                    hsv.z += step;
                    brightness = weighted_w3c(hsv_to_rgb(hsv));
                }
            }
            else
            {
                while (brightness > bright_cap and hsv.z >= 0.f and hsv.z <= 1.f)
                {
                    hsv.z -= step;
                    brightness = weighted_w3c(hsv_to_rgb(hsv));
                }
            }

            return hsv_to_rgb(hsv);
        }

        FeedColors feed_colors_instance =
        {
            .info    = hex_to_vec4f(0xD4D4D4FF),
            .warning = hex_to_vec4f(0xE3811CFF),
            .error   = hex_to_vec4f(0xFF0000FF),
        };

        FeedColors feed_colors_inverse_instance;

        // For supporting color inversion mode toggling.
        FeedColors* current_feed_colors_instance = &feed_colors_instance;

        FeedState feed_state_instance =
        {
            .feed_font_size = 24,
        };

        WidgetColors widget_colors_instance =
        {
            .window_border             = hex_to_vec4f(0xADD6FF26),
            .window_title_background   = hex_to_vec4f(0xADD6FF26),
            .window_title_font_color   = hex_to_vec4f(0xD4D4D4FF),
            .window_close_button_hover = hex_to_vec4f(0xF44747FF),
            .scrollbar_inactive        = hex_to_vec4f(0xADD6FF26),
            .scrollbar_active          = hex_to_vec4f(0xADD6FFFF),
            .scrollbar_track_outline   = hex_to_vec4f(0xADD6FF26),
        };

        WidgetColors widget_colors_inverse_instance;

        // For supporting color inversion mode toggling.
        WidgetColors* current_widget_colors_instance = &widget_colors_instance;

        SystemCore system_core_instance =
        {
            // This will always be set on creation.
            .base_asset_path = "",
        };

        SystemFonts system_fonts_instance =
        {
            // TODO: We should probably use a default that isn't so... Windows-y.
            .fallback_fonts_folder = "C:\\Windows\\Fonts",
            .current_font = "../fonts/iosevka-regular.ttf",
        };

        SystemEffects system_effects_instance =
        {
            .postprocessing_enabled = true,
            .screen_warp = true,
            .multipass_crt = true,
            .crt_mode = false,
            .light_mode = false,
        };

        SystemColors system_colors_instance =
        {
            .background         = hex_to_vec4f(0x1F1F1FFF),
            .default_font_color = hex_to_vec4f(0xD4D4D4FF),
        };

        SystemColors system_colors_inverse_instance;

        // For supporting color inversion mode toggling.
        SystemColors* current_system_colors_instance = &system_colors_instance;

        bool need_save = false;

        template <typename C, typename M>
        struct MemberContainer
        {
            M C::*pm;
            size_t offset;
            const char* member_name;

            using MemberType = M;
        };

        // Note: I initially tried to use std::tuple instead of a lambda closure class, but
        // it turns out that the std::tuple implementation will not pack properly if there
        // are holes in a structure followed by full-width members.  e.g.:
        // [uint32, uint8, uint8, uint32]
        // will get packed as 16 instead of the expected 12 like a normal C++ struct would.
        // Luckily, we can rely on the fact that explicit captures of a lambda create a standard
        // layout structure with the members packed together.  Even though
        // [expr.prim.lambda.capture]/10 tells us the order is unspecified, it is a common
        // implementation tactic to pack them as close as possible.
        template <typename... Ts>
        constexpr size_t sizeof_pack()
        {
            auto l = [...pack = Ts()]{ (void)(pack, ...); };
            return sizeof l;
        }

        template <typename T, MemberContainer... Containers>
        constexpr bool validate()
        {
            constexpr size_t offsets[] = { Containers.offset... };
            // Ensure that each member is in sorted order.
            if constexpr (not std::is_sorted(std::begin(offsets), std::end(offsets)))
                throw "check decl order";
            if constexpr (sizeof(T) != sizeof_pack<typename decltype(Containers)::MemberType...>())
                throw "check structure layout";
            return true;
        }

        using TOMLView = toml::node_view<const toml::node>;

        template <MemberContainer C>
        void toml_fill_single(FeedColors* data, TOMLView source)
        {
            auto value = source[C.member_name].value<uint32_t>();
            if (value)
            {
                ((*data).*(C.pm)) = hex_to_vec4f(*value);
            }
        }

        template <MemberContainer C>
        void toml_fill_single(FeedState* data, TOMLView source)
        {
            auto value = source[C.member_name].value<typename decltype(C)::MemberType>();
            if (value)
            {
                ((*data).*(C.pm)) = *value;
            }
        }

        template <MemberContainer C>
        void toml_fill_single(WidgetColors* data, TOMLView source)
        {
            auto value = source[C.member_name].value<uint32_t>();
            if (value)
            {
                ((*data).*(C.pm)) = hex_to_vec4f(*value);
            }
        }

        template <MemberContainer C>
        void toml_fill_single(SystemCore* data, TOMLView source)
        {
            auto value = source[C.member_name].value<typename decltype(C)::MemberType>();
            if (value)
            {
                ((*data).*(C.pm)) = *value;
            }
        }

        template <MemberContainer C>
        void toml_fill_single(SystemFonts* data, TOMLView source)
        {
            auto value = source[C.member_name].value<std::string>();
            if (value)
            {
                ((*data).*(C.pm)) = *value;
            }
        }

        template <MemberContainer C>
        void toml_fill_single(SystemEffects* data, TOMLView source)
        {
            auto value = source[C.member_name].value<typename decltype(C)::MemberType>();
            if (value)
            {
                ((*data).*(C.pm)) = *value;
            }
        }

        template <MemberContainer C>
        void toml_fill_single(SystemColors* data, TOMLView source)
        {
            auto value = source[C.member_name].value<uint32_t>();
            if (value)
            {
                ((*data).*(C.pm)) = hex_to_vec4f(*value);
            }
        }

        template <typename T, MemberContainer... Containers>
        void fill_impl(T* data, TOMLView source)
        {
            static_assert(validate<T, Containers...>());
            (void)(toml_fill_single<Containers>(data, source), ...);
        }

        using TOMLTable = toml::table;
        using TOMLArray = toml::array;

        template <MemberContainer C>
        void toml_save_single(const FeedColors& data, TOMLTable* save_to)
        {
            auto value = (data.*(C.pm));
            save_to->insert(C.member_name, vec4f_to_hex(value), toml::value_flags::format_as_hexadecimal);
        }

        template <MemberContainer C>
        void toml_save_single(const FeedState& data, TOMLTable* save_to)
        {
            auto value = (data.*(C.pm));
            save_to->insert(C.member_name, value);
        }

        template <MemberContainer C>
        void toml_save_single(const WidgetColors& data, TOMLTable* save_to)
        {
            auto value = (data.*(C.pm));
            save_to->insert(C.member_name, vec4f_to_hex(value), toml::value_flags::format_as_hexadecimal);
        }

        template <MemberContainer C>
        void toml_save_single(const SystemCore& data, TOMLTable* save_to)
        {
            auto value = (data.*(C.pm));
            save_to->insert(C.member_name, value);
        }

        template <MemberContainer C>
        void toml_save_single(const SystemFonts& data, TOMLTable* save_to)
        {
            auto value = (data.*(C.pm));
            save_to->insert(C.member_name, value);
        }

        template <MemberContainer C>
        void toml_save_single(const SystemEffects& data, TOMLTable* save_to)
        {
            auto value = (data.*(C.pm));
            save_to->insert(C.member_name, value);
        }

        template <MemberContainer C>
        void toml_save_single(const SystemColors& data, TOMLTable* save_to)
        {
            auto value = (data.*(C.pm));
            save_to->insert(C.member_name, vec4f_to_hex(value), toml::value_flags::format_as_hexadecimal);
        }

        template <typename T, MemberContainer... Containers>
        void save_impl(const T& data, TOMLTable* save_to)
        {
            static_assert(validate<T, Containers...>());
            (void)(toml_save_single<Containers>(data, save_to), ...);
        }

        // Unfortunately macros are the only way for us to effectively generate serialization code without intrusively
        // injecting some code into the containing structure.
#define CONCATENATE(arg1, arg2)   CONCATENATE1(arg1, arg2)
#define CONCATENATE1(arg1, arg2)  CONCATENATE2(arg1, arg2)
#define CONCATENATE2(arg1, arg2)  arg1##arg2

#define FOR_EACH_1(T, what, x, ...)  what(T, x)
#define FOR_EACH_2(T, what, x, ...)  what(T, x), FOR_EACH_1(T, what,  __VA_ARGS__)
#define FOR_EACH_3(T, what, x, ...)  what(T, x), FOR_EACH_2(T, what, __VA_ARGS__)
#define FOR_EACH_4(T, what, x, ...)  what(T, x), FOR_EACH_3(T, what,  __VA_ARGS__)
#define FOR_EACH_5(T, what, x, ...)  what(T, x), FOR_EACH_4(T, what,  __VA_ARGS__)
#define FOR_EACH_6(T, what, x, ...)  what(T, x), FOR_EACH_5(T, what,  __VA_ARGS__)
#define FOR_EACH_7(T, what, x, ...)  what(T, x), FOR_EACH_6(T, what,  __VA_ARGS__)
#define FOR_EACH_8(T, what, x, ...)  what(T, x), FOR_EACH_7(T, what,  __VA_ARGS__)
#define FOR_EACH_9(T, what, x, ...)  what(T, x), FOR_EACH_8(T, what,  __VA_ARGS__)
#define FOR_EACH_10(T, what, x, ...) what(T, x), FOR_EACH_9(T, what,  __VA_ARGS__)
#define FOR_EACH_11(T, what, x, ...) what(T, x), FOR_EACH_10(T, what,  __VA_ARGS__)
#define FOR_EACH_12(T, what, x, ...) what(T, x), FOR_EACH_11(T, what,  __VA_ARGS__)
#define FOR_EACH_13(T, what, x, ...) what(T, x), FOR_EACH_12(T, what,  __VA_ARGS__)
#define FOR_EACH_14(T, what, x, ...) what(T, x), FOR_EACH_13(T, what,  __VA_ARGS__)
#define FOR_EACH_15(T, what, x, ...) what(T, x), FOR_EACH_14(T, what,  __VA_ARGS__)
#define FOR_EACH_16(T, what, x, ...) what(T, x), FOR_EACH_15(T, what,  __VA_ARGS__)
#define FOR_EACH_17(T, what, x, ...) what(T, x), FOR_EACH_16(T, what,  __VA_ARGS__)
#define FOR_EACH_18(T, what, x, ...) what(T, x), FOR_EACH_17(T, what,  __VA_ARGS__)
#define FOR_EACH_19(T, what, x, ...) what(T, x), FOR_EACH_18(T, what,  __VA_ARGS__)
#define FOR_EACH_20(T, what, x, ...) what(T, x), FOR_EACH_19(T, what,  __VA_ARGS__)

#define FOR_EACH_NARG(...) FOR_EACH_NARG_(__VA_ARGS__, FOR_EACH_RSEQ_N())
#define FOR_EACH_NARG_(...) FOR_EACH_ARG_N(__VA_ARGS__)
#define FOR_EACH_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, N, ...) N
#define FOR_EACH_RSEQ_N() 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

#define FOR_EACH_(T, N, what, x, ...) CONCATENATE(FOR_EACH_, N)(T, what, x, __VA_ARGS__)
#define FOR_EACH(T, what, x, ...) FOR_EACH_(T, FOR_EACH_NARG(x __VA_OPT__(, __VA_ARGS__)), what, x, __VA_ARGS__)

#define GENERATE_MEMBER_CONTAINER(T, x) MemberContainer{ &T::x, __builtin_offsetof(T, x), #x }

#define GENERATE_SERIALIZE(T, ...) void serialize_fill(T* data, TOMLView source) {          \
        fill_impl<T, FOR_EACH(T, GENERATE_MEMBER_CONTAINER, __VA_ARGS__)>(data, source); }  \
    void serialize_save(const T& data, TOMLTable* save_to) {                                \
        save_impl<T, FOR_EACH(T, GENERATE_MEMBER_CONTAINER, __VA_ARGS__)>(data, save_to); } \

//#define GENERATE_MEMBER_ASSIGN(T, M) inverse->M = invert_color(src.M)
#define GENERATE_MEMBER_ASSIGN(T, M) inverse->M = readable_color_for_any_bg(src.M)

#define GENERATE_INVERSION(T, ...) void populate_inversion(const T& src, T* inverse) { \
        FOR_EACH(T, GENERATE_MEMBER_ASSIGN, __VA_ARGS__); }                            \

#define GENERATE_SERIALIZE_AND_INVERSION(T, ...) GENERATE_SERIALIZE(T, __VA_ARGS__); GENERATE_INVERSION(T, __VA_ARGS__)

        GENERATE_SERIALIZE_AND_INVERSION(FeedColors,
                                            info,
                                            warning,
                                            error);

        constexpr std::string_view feed_colors_path = "feed.colors";

        GENERATE_SERIALIZE(FeedState,
                            feed_font_size);

        constexpr std::string_view feed_state_path = "feed.state";

        GENERATE_SERIALIZE_AND_INVERSION(WidgetColors,
                                            window_border,
                                            window_title_background,
                                            window_title_font_color,
                                            window_close_button_hover,
                                            scrollbar_inactive,
                                            scrollbar_active,
                                            scrollbar_track_outline);

        constexpr std::string_view widget_colors_path = "widget.colors";

        GENERATE_SERIALIZE(SystemCore,
                            base_asset_path);

        constexpr std::string_view system_core_path = "system.core";

        GENERATE_SERIALIZE(SystemFonts,
                            fallback_fonts_folder,
                            current_font);

        constexpr std::string_view system_fonts_path = "system.fonts";

        GENERATE_SERIALIZE(SystemEffects,
                            postprocessing_enabled,
                            screen_warp,
                            multipass_crt,
                            crt_mode,
                            light_mode);

        constexpr std::string_view system_effects_path = "system.effects";

        GENERATE_SERIALIZE_AND_INVERSION(SystemColors,
                                            background,
                                            default_font_color);

        constexpr std::string_view system_colors_path = "system.colors";

        // These have to be defined after since they rely on the helpers generated above.
        void populate_inverse_color_states()
        {
            populate_inversion(feed_colors_instance, &feed_colors_inverse_instance);
            current_feed_colors_instance = &feed_colors_instance;

            populate_inversion(widget_colors_instance, &widget_colors_inverse_instance);
            current_widget_colors_instance = &widget_colors_instance;

            populate_inversion(system_colors_instance, &system_colors_inverse_instance);
            // We really do want to invert the background to get a true inverted light-mode color.
            system_colors_inverse_instance.background = invert_color(system_colors_instance.background);
            // Same for the font color.
            system_colors_inverse_instance.default_font_color = invert_color(system_colors_instance.default_font_color);
            current_system_colors_instance = &system_colors_instance;
            if (system_effects().light_mode)
            {
                current_feed_colors_instance = &feed_colors_inverse_instance;
                current_widget_colors_instance = &widget_colors_inverse_instance;
                current_system_colors_instance = &system_colors_inverse_instance;
            }
        }
    } // namespace [anon]

    const FeedColors& feed_colors()
    {
        return *current_feed_colors_instance;
    }

    const FeedState& feed_state()
    {
        return feed_state_instance;
    }

    const WidgetColors& widget_colors()
    {
        return *current_widget_colors_instance;
    }

    const SystemColors& system_colors()
    {
        return *current_system_colors_instance;
    }

    const SystemCore& system_core()
    {
        return system_core_instance;
    }

    const SystemFonts& system_fonts()
    {
        return system_fonts_instance;
    }

    const SystemEffects& system_effects()
    {
        return system_effects_instance;
    }

    bool needs_save()
    {
        return need_save;
    }

    void update(const SystemCore& new_state)
    {
        system_core_instance = new_state;
        need_save = true;
    }

    void update(const SystemFonts& new_state)
    {
        system_fonts_instance = new_state;
        need_save = true;
    }

    void update(const SystemEffects& new_state)
    {
        system_effects_instance = new_state;
        need_save = true;
    }

    bool load_config(std::string_view path, Feed::MessageFeed* feed)
    {
        std::u8string_view utf8_path = { reinterpret_cast<const char8_t*>(path.data()), path.size() };

        try
        {
            const auto config = toml::parse_file(utf8_path);

            auto feed_colors = config.at_path(feed_colors_path);
            serialize_fill(&feed_colors_instance, feed_colors);

            auto feed_state = config.at_path(feed_state_path);
            serialize_fill(&feed_state_instance, feed_state);

            auto widget_colors = config.at_path(widget_colors_path);
            serialize_fill(&widget_colors_instance, widget_colors);

            auto system_core = config.at_path(system_core_path);
            serialize_fill(&system_core_instance, system_core);

            auto system_fonts = config.at_path(system_fonts_path);
            serialize_fill(&system_fonts_instance, system_fonts);

            auto system_effects = config.at_path(system_effects_path);
            serialize_fill(&system_effects_instance, system_effects);

            auto system_colors = config.at_path(system_colors_path);
            serialize_fill(&system_colors_instance, system_colors);
        }
        catch (const toml::parse_error& err)
        {
            auto msg = std::format("failed to parse config file: {}", err.description().data());
            feed->queue_error(msg);
            return false;
        }

        populate_inverse_color_states();
        return true;
    }

    bool save_config(std::string_view path, Feed::MessageFeed* feed)
    {
        // Regardless of if the save is successful or not, we should tell the rest of the app that we do not need to save anymore.
        need_save = false;

        auto root = TOMLTable{ };

        // feed.* values.
        {
            auto [feed_tbl, ins1] = root.emplace<TOMLTable>("feed");
            // feed.colors
            {
                auto [feed_colors_tbl, ins2] = feed_tbl->second.as<TOMLTable>()->emplace<TOMLTable>("colors");
                serialize_save(feed_colors_instance, feed_colors_tbl->second.as<TOMLTable>());
            }
            // feed.state
            {
                auto [feed_state_tbl, ins2] = feed_tbl->second.as<TOMLTable>()->emplace<TOMLTable>("state");
                serialize_save(feed_state_instance, feed_state_tbl->second.as<TOMLTable>());
            }
        }

        // widget.* values.
        {
            auto [widget_tbl, ins1] = root.emplace<TOMLTable>("widget");
            // widget.colors
            {
                auto [widget_colors_tbl, ins2] = widget_tbl->second.as<TOMLTable>()->emplace<TOMLTable>("colors");
                serialize_save(widget_colors_instance, widget_colors_tbl->second.as<TOMLTable>());
            }
        }

        // system.* values.
        {
            auto [system_tbl, ins1] = root.emplace<TOMLTable>("system");
            // system.core
            {
                auto [system_core_tbl, ins2] = system_tbl->second.as<TOMLTable>()->emplace<TOMLTable>("core");
                serialize_save(system_core_instance, system_core_tbl->second.as<TOMLTable>());
            }
            // system.fonts
            {
                auto [system_fonts_tbl, ins2] = system_tbl->second.as<TOMLTable>()->emplace<TOMLTable>("fonts");
                serialize_save(system_fonts_instance, system_fonts_tbl->second.as<TOMLTable>());
            }
            // system.effects
            {
                auto [system_effects_tbl, ins2] = system_tbl->second.as<TOMLTable>()->emplace<TOMLTable>("effects");
                serialize_save(system_effects_instance, system_effects_tbl->second.as<TOMLTable>());
            }
            // system.colors
            {
                auto [system_colors_tbl, ins2] = system_tbl->second.as<TOMLTable>()->emplace<TOMLTable>("colors");
                serialize_save(system_colors_instance, system_colors_tbl->second.as<TOMLTable>());
            }
        }

        toml::toml_formatter formatter{ root, toml::format_flags::allow_hexadecimal_integers | toml::format_flags::allow_literal_strings };
        std::stringstream ss;
        ss << formatter;

        auto buf = ss.str();
        auto err = save_file(path.data(), buf);
        if (err != Errno::OK)
        {
            char msg[512];
            strerror_s(msg, rep(err));
            auto txt = std::format("Failed to save to '{}': {}", path.data(), msg);
            feed->queue_info(txt);
            return false;
        }
        return true;
    }
} // namespace Config