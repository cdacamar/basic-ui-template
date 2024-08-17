#include "window-theming.h"

#include <system_error>

#include <dwmapi.h>

#include "config.h"
#include "feed.h"
#include "scoped-handle.h"
#include "util.h"
#include "vec.h"

namespace Theme
{
    namespace
    {
        COLORREF as_colorref(const Vec4f& color)
        {
            // Our hex is in the form: RRGGBBAA.
            // COLORREF is expecting:  00BBGGRR.
            // To perform the conversion we will first chop the alpha
            // then select the bits we need.
            auto hex = color_rgb(color);
            // Our color is now: 00RRGGBB.
            COLORREF result = RGB((hex & 0x00FF0000) >> 16,
                                  (hex & 0x0000FF00) >> 8,
                                  hex & 0x000000FF);
            return result;
        }

        struct ModuleFree
        {
            void operator()(HMODULE module)
            {
                if (module != nullptr)
                {
                    FreeLibrary(module);
                }
            }
        };

        using ScopedModuleHandle = ScopedHandle<HMODULE, ModuleFree>;

        using DwmSetWindowAttributeFunc = HRESULT(*)(HWND, DWORD, LPCVOID, DWORD);

        // We want to lazily load the DWM module and test if our API is available.
        struct DWMModule
        {
            ScopedModuleHandle handle;
            DwmSetWindowAttributeFunc attr_func = nullptr;
        };

        DWMModule dwm_module;

        void load_dwm(DWMModule* mod, Feed::MessageFeed* feed)
        {
            HMODULE dwm = LoadLibraryA("dwmapi.dll");
            if (dwm == nullptr)
            {
                feed->queue_error("Unable to load DWM module.");
                feed->queue_error("Window theming will not work.");
                return;
            }
            mod->handle = ScopedModuleHandle{ dwm };
            mod->attr_func = reinterpret_cast<DwmSetWindowAttributeFunc>(GetProcAddress(mod->handle.handle(), "DwmSetWindowAttribute"));
            if (mod->attr_func == nullptr)
            {
                feed->queue_error("Unable to retrieve 'DwmSetWindowAttribute' in DWM module.");
                feed->queue_error("Window theming will not work.");
            }
        }
    } // namespace [anon]

    void init(Feed::MessageFeed* feed)
    {
        load_dwm(&dwm_module, feed);
    }

    // Largely borrowed from: https://stackoverflow.com/questions/39261826/change-the-color-of-the-title-bar-caption-of-a-win32-application.
    void apply_boarder_color(OpaqueWindow window, Feed::MessageFeed* feed)
    {
        if (dwm_module.attr_func == nullptr)
            return;
        HWND wnd = static_cast<HWND>(window.value);

        COLORREF color = as_colorref(Config::system_colors().background);
        auto cap_color = dwm_module.attr_func(
            wnd,
            DWMWINDOWATTRIBUTE::DWMWA_CAPTION_COLOR,
            &color,
            sizeof(color));

        if (not SUCCEEDED(cap_color))
        {
            auto str = std::system_category().message(cap_color);
            feed->queue_error("Could not enable window colors");
            feed->queue_error(str);
            return;
        }

        color = as_colorref(Config::system_colors().default_font_color);
        cap_color = dwm_module.attr_func(
            wnd,
            DWMWINDOWATTRIBUTE::DWMWA_TEXT_COLOR,
            &color,
            sizeof(color));

        if (not SUCCEEDED(cap_color))
        {
            auto str = std::system_category().message(cap_color);
            feed->queue_error("Could not enable window colors");
            feed->queue_error(str);
        }
    }
} // namespace Theme