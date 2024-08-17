#pragma once

#include <string_view>
#include <string>
#include <vector>

#include "vec.h"

namespace Feed
{
    class MessageFeed;
} // namespace Feed

namespace Config
{
    struct FeedColors
    {
        Vec4f info;
        Vec4f warning;
        Vec4f error;
    };

    struct FeedState
    {
        int feed_font_size;
    };

    struct WidgetColors
    {
        Vec4f window_border;
        Vec4f window_title_background;
        Vec4f window_title_font_color;
        Vec4f window_close_button_hover;
        Vec4f scrollbar_inactive;
        Vec4f scrollbar_active;
        Vec4f scrollbar_track_outline;
    };

    struct SystemCore
    {
        std::string base_asset_path;
    };

    struct SystemFonts
    {
        std::string fallback_fonts_folder;
        std::string current_font;
    };

    struct SystemEffects
    {
        bool postprocessing_enabled;
        bool screen_warp;
        bool multipass_crt;
        bool crt_mode;
        bool light_mode;
    };

    struct SystemColors
    {
        Vec4f background;
        Vec4f default_font_color;
    };

    // Queries.
    const FeedColors& feed_colors();
    const FeedState& feed_state();
    const WidgetColors& widget_colors();
    const SystemCore& system_core();
    const SystemFonts& system_fonts();
    const SystemEffects& system_effects();
    const SystemColors& system_colors();
    bool needs_save();

    // Updates.
    void update(const SystemCore& new_state);
    void update(const SystemFonts& new_state);
    void update(const SystemEffects& new_state);

    // File handling.
    bool load_config(std::string_view path, Feed::MessageFeed* feed);
    bool save_config(std::string_view path, Feed::MessageFeed* feed);
} // namespace Config