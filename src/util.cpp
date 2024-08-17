#include "util.h"

#include <stdio.h>

#include <cmath>

#include <filesystem>

#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL.h>
#include <Windows.h>

//#include "blake2b-ref.c"

#include "scoped-handle.h"

namespace
{
    Errno file_size(FILE* file, size_t* size)
    {
        long pos = ftell(file);
        if (pos < 0)
            return Errno{ errno };
        if (fseek(file, 0, SEEK_END) < 0)
            return Errno{ errno};
        long result = ftell(file);
        if (result < 0)
            return Errno{ errno};
        if (fseek(file, pos, SEEK_SET) < 0)
            return Errno{ errno};
        *size = (size_t) result;
        return { };
    }

    struct CloseSimpleFileHandle
    {
        void operator()(FILE* file) const
        {
            fclose(file);
        }
    };

    using SimpleFileHandle = ScopedHandle<FILE*, CloseSimpleFileHandle>;

    constexpr bool is_slash(char c)
    {
        return c == '\\' or c == '/';
    }

    struct WindowsPlatformInfo
    {
        HWND window;
    };

    WindowsPlatformInfo platform_info;
} // namespace [anon]

Errno read_file(std::string_view file_path, std::string* buf)
{
    FILE* file = nullptr;

    auto error = Errno{ fopen_s(&file, file_path.data(), "rb") };
    if (error != Errno::OK)
        return error;

    SimpleFileHandle guard{ file };

    size_t size = 0;
    Errno err = file_size(file, &size);
    if (err != Errno::OK)
        return err;

    buf->resize(size);

    fread(buf->data(), size, 1, file);
    if (ferror(file))
        return Errno{ errno };

    return Errno::OK;
}

Errno save_file(std::string_view file_path, const std::string& buf)
{
    FILE* file = nullptr;

    auto error = Errno{ fopen_s(&file, file_path.data(), "wb") };
    if (error != Errno::OK)
        return error;

    SimpleFileHandle guard{ file };

    fwrite(buf.data(), sizeof(char), buf.size(), file);
    if (ferror(file))
        return Errno{ errno };

    return Errno::OK;
}

bool file_exists(std::string_view file_path)
{
    std::error_code ec;
    return std::filesystem::exists(file_path, ec);
}

bool regular_file(std::string_view file_path)
{
    if (not file_exists(file_path))
        return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(file_path, ec);
}

bool dir_exists(std::string_view dir_path)
{
    if (dir_path.empty())
        return false;
    std::error_code ec;
    std::filesystem::path p = dir_path;
    if (not std::filesystem::exists(p, ec))
        return false;
    return std::filesystem::is_directory(p, ec);
}

std::string working_dir()
{
    std::error_code ec;
    auto path = std::filesystem::current_path(ec);
    // Convert to UTF8.
    auto str = path.u8string();
    return { reinterpret_cast<const char*>(str.c_str()), str.size() };
}

Errno set_working_dir(const char* file_path)
{
    std::error_code ec;
    std::filesystem::path p = reinterpret_cast<const char8_t*>(file_path);
    if (not std::filesystem::is_directory(p))
        p = p.parent_path();
    std::filesystem::current_path(p, ec);
    // We should test this...
    return Errno::OK;
}

std::string combine_paths(std::string_view a, std::string_view b)
{
    std::filesystem::path path_a{ reinterpret_cast<const char8_t*>(a.data()) };
    std::filesystem::path path_b{ reinterpret_cast<const char8_t*>(b.data()) };
    auto combined = path_a / path_b;
    // Convert to UTF8.
    auto str = combined.u8string();
    return { reinterpret_cast<const char*>(str.c_str()), str.size() };
}

// The filename component is the final component along the path.  Let's find
// the final 'slash' and return the view to the end.
std::string_view filename(const std::string_view path)
{
    auto first = rbegin(path);
    auto last = rend(path);
    auto found = std::find_if(first, last, is_slash);
    // No slashes?  Must be relative, return the whole thing.
    if (found == last)
        return path;
    auto first_base = found.base();
    return path.substr(std::distance(begin(path), first_base));
}

std::string default_font_path(std::string_view core_asset_path)
{
    return combine_paths(core_asset_path, "../fonts");
}

std::string default_config_directory()
{
    // Not really sure what my 'org' is, but I'll just use my alias for now...
    char* user_path = SDL_GetPrefPath("cadacama", "basic-ui-template");
    std::filesystem::path p = user_path;
    SDL_free(user_path);
    p /= "config.toml";
    // Convert to UTF8.
    auto str = p.u8string();
    return { reinterpret_cast<const char*>(str.c_str()), str.size() };
}

void set_platform_window(OpaqueWindow window)
{
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    auto* sdl_window = static_cast<SDL_Window*>(window.value);
    SDL_GetWindowWMInfo(sdl_window, &info);
    HWND wnd = info.info.win.window;

    platform_info.window = wnd;
}

OpaqueWindow get_platform_window()
{
    return OpaqueWindow{ platform_info.window };
}

void setup_platform_dpi()
{
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

DPI get_platform_dpi()
{
    return DPI{ GetDpiForWindow(platform_info.window) };
}

float get_platform_dpi_pixel_ratio()
{
    DPI dpi = get_platform_dpi();
    if (rep(dpi) == 0)
        return 1.f;
    constexpr float standard_dpi = 96.f;
    return standard_dpi / rep(dpi);
}

void files_in_dir(std::string_view str_dir, FilesInDirResult* result, std::string_view ext_filter)
{
    std::error_code ec;
    auto dir = std::filesystem::canonical(reinterpret_cast<const char8_t*>(str_dir.data()), ec);
    if (ec)
        return;
    if (not std::filesystem::is_directory(dir, ec))
        return;
    auto itr = std::filesystem::directory_iterator{ dir, ec };
    if (ec)
        return;
    result->clear();
    auto converted_ext = std::u8string_view{ reinterpret_cast<const char8_t*>(ext_filter.data()), ext_filter.size() };
    for (const auto& entry : itr)
    {
        if (entry.is_regular_file())
        {
            // If this does not match the desired extension, skip it.
            if (not converted_ext.empty()
                and entry.path().extension().u8string() != converted_ext)
            {
                continue;
            }
            auto utf8_str = entry.path().u8string();
            result->push_back({ reinterpret_cast<const char*>(utf8_str.c_str()), utf8_str.size() });
        }
    }
}

Ticks ticks_since_app_start()
{
    return Ticks{ SDL_GetTicks() };
}

bool delta_meets_double_click_time(Ticks start, Ticks end)
{
    if (start > end)
        return false;
    auto double_click_time = GetDoubleClickTime();
    return ((rep(end) - rep(start)) <= double_click_time);
}

// Hashing.
#if 0
bool hash_bytes(HashInput in, HashResult* out)
{
    std::memset(out, sizeof(HashResult), 0);
    // This API returns non-zero if there was an error.
    return blake2b(reinterpret_cast<uint8_t*>(&out->result[0]),
                    sizeof(HashResult),
                    in.bytes,
                    in.len,
                    nullptr,
                    0) == 0;
}
#endif

size_t digits(size_t n)
{
    return static_cast<size_t>(std::floor(std::log10(n) + 1));
}