#pragma once

#include <concepts>
#include <string_view>
#include <string>
#include <vector>

#include "types.h"

enum class Errno : int { OK };

// File handling.
Errno read_file(std::string_view file_path, std::string* buf);
Errno save_file(std::string_view file_path, const std::string& buf);
bool file_exists(std::string_view file_path);
bool regular_file(std::string_view file_path);
bool dir_exists(std::string_view dir_path);
std::string working_dir();
Errno set_working_dir(const char* file_path);
std::string combine_paths(std::string_view a, std::string_view b);
std::string_view filename(std::string_view path);
std::string default_font_path(std::string_view core_asset_path);
std::string default_config_directory();

using FilesInDirResult = std::vector<std::string>;
void files_in_dir(std::string_view dir, FilesInDirResult* result, std::string_view ext_filter = { });

// Window requests.
void set_platform_window(OpaqueWindow window);
OpaqueWindow get_platform_window();
void setup_platform_dpi();

// DPI requests.
enum class DPI : uint32_t { };
DPI get_platform_dpi();
float get_platform_dpi_pixel_ratio();

// Timing.
enum class Ticks : unsigned int { };
Ticks ticks_since_app_start();
bool delta_meets_double_click_time(Ticks start, Ticks end);

// Hashing.
struct HashResult
{
    uint64_t result[2];

    bool operator==(const HashResult&) const = default;
};

struct HashInput
{
    const uint8_t* bytes;
    size_t len;
};

bool hash_bytes(HashInput in, HashResult* out);

template <typename T>
HashInput as_hash_input(const T& x)
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&x);
    return { .bytes = bytes, .len = sizeof(T) };
}

// General.
template <typename T, typename U>
concept Lerpable = requires(T s, U t) {
    { 1 - t };
    { s * t } -> std::convertible_to<T>;
    { s + s };
};

template <typename T, typename U>
requires Lerpable<T, U>
inline auto lerp(const T& start, const T& end, const U& mixin)
{
    return start * (1 - mixin) + end * mixin;
}

size_t digits(size_t n);