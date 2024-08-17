#include "glyph-cache.h"

#include <algorithm>
#include <format>
#include <unordered_map>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "config.h"
#include "enum-utils.h"
#include "feed.h"
#include "scoped-handle.h"
#include "utf-8.h"
#include "util.h"

// Shamelessly stolen :)
// https://en.wikibooks.org/wiki/OpenGL_Programming/Modern_OpenGL_Tutorial_Text_Rendering_02

namespace Glyph
{
    namespace
    {
        struct CharInfo
        {
            float ax; // advance.x
            float ay; // advance.y

            float bw; // bitmap.width;
            float bh; // bitmap.rows;

            float bl; // bitmap_left;
            float bt; // bitmap_top;

            float tx; // x offset of glyph in texture coordinates
            float ty; // y offset of glyph in texture coordinates
        };

        constexpr int MarkerGlyphCount = 3;
        constexpr int ValidCharStart = 32;
        constexpr int CharInfoCount = 128;
        constexpr int TotalCharInfoCount = CharInfoCount + MarkerGlyphCount;

        enum class SpecialGlyph : UTF8::Codepoint
        {
            Whitespace = CharInfoCount,
            CarriageReturn,
            Tab,

            Count
        };

        static_assert(count_of<SpecialGlyph> == TotalCharInfoCount);

        struct SpecialGlyphEntry
        {
            SpecialGlyph index;
            FT_ULong glyph;
        };

        constexpr SpecialGlyphEntry special_glyph_map[count_of<SpecialGlyph> - CharInfoCount] = {
            { .index = SpecialGlyph::Whitespace, .glyph = 0x00B7 },
            { .index = SpecialGlyph::CarriageReturn, .glyph = 0x00B6 },
            { .index = SpecialGlyph::Tab, .glyph = 0x2192 },
        };

        static_assert(std::is_sorted(std::begin(special_glyph_map),
                                        std::end(special_glyph_map),
                                        [](const auto& a, const auto& b)
                                        {
                                            return rep(a.index) <  rep(b.index);
                                        }));

        struct FTLibraryCleanup
        {
            void operator()(FT_Library lib) const
            {
                if (lib != nullptr)
                {
                    FT_Done_FreeType(lib);
                }
            }
        };

        using FTLibraryHandle = ScopedHandle<FT_Library, FTLibraryCleanup>;

        struct FTFaceCleanup
        {
            void operator()(FT_Face face) const
            {
                if (face != nullptr)
                {
                    FT_Done_Face(face);
                }
            }
        };

        using FTFaceHandle = ScopedHandle<FT_Face, FTFaceCleanup>;

        constexpr FT_UInt texture_width = 1920;
        constexpr FT_UInt texture_height = 1088;

        struct UnicodeGlyphInfo
        {
            CharInfo info;
            FT_Face face;
            bool rasterized = false;
            bool failed_to_rasterize = false;
        };

        using UnicodeGlyphMap = std::unordered_map<UTF8::Codepoint, UnicodeGlyphInfo>;

        using FallbackFontCache = std::vector<FTFaceHandle>;
    } // namespace [anon]

    struct CachedFont
    {
        int font_size = 64;
        UnicodeGlyphMap cached_glyphs_map;
        CharInfo infos[TotalCharInfoCount];
    };

    using CachedFontsMap = std::unordered_map<int, CachedFont>;

    struct Atlas::Data
    {
        static constexpr int default_font_size = 64;

        FTLibraryHandle library;

        FTFaceHandle face;

        FT_UInt height{};
        FT_UInt width{};

        // For caching glyphs on the fly.
        FT_UInt unicode_row_start{};
        FT_UInt next_x{};
        FT_UInt next_y{};
        FT_UInt cur_row_max_height{};
        FallbackFontCache fallback_fonts;
        CachedFont* selected_font;
        CachedFontsMap cached_fonts;

        Render::GlyphTexture texture{};
    };

    namespace
    {
        FT_Face identify_font_face_for_glyph(Atlas::Data* data, UTF8::Codepoint glyph)
        {
            // Try the most obvious spot first, the font currently selected.
            auto idx = FT_Get_Char_Index(data->face.handle(), glyph);
            if (idx != 0)
                return data->face.handle();
            // Need to load the fallback fonts.
            if (data->fallback_fonts.empty())
            {
                // Now we need to try fallback fonts.
                // Do the dumb thing for now and load them all.
                FilesInDirResult files;
                files_in_dir(Config::system_fonts().fallback_fonts_folder, &files, ".ttf");
                data->fallback_fonts.reserve(files.size() + 1);
                // Insert a sentinel value to avoid reloading this.
                data->fallback_fonts.push_back({ });
                // Try to load each face.
                for (const auto& file : files)
                {
                    FT_Face face{ };
                    auto error = FT_New_Face(data->library.handle(), file.c_str(), 0, &face);
                    if (error != 0)
                    {
                        const char* log = FT_Error_String(error);
                        fprintf(stderr, "Failed to load fallback font file '%s': %s\n", file.c_str(), log);
                        continue;
                    }

                    auto new_face = FTFaceHandle{ face };

                    constexpr FT_UInt pixel_size = Atlas::Data::default_font_size;
                    // Width == 0.  We don't want bold fonts.
                    error = FT_Set_Pixel_Sizes(new_face.handle(), 0, pixel_size);
                    if (error != 0)
                    {
                        const char* log = FT_Error_String(error);
                        fprintf(stderr, "Failed to set font size on fallback font: %s\n", log);
                        continue;
                    }
                    // We've loaded this face, we can now insert it.
                    data->fallback_fonts.push_back(std::move(new_face));
                }
            }

            // In the fallback fonts, try to find the face which could rasterize this glyph.
            for (const auto& face : data->fallback_fonts)
            {
                // This is the sentinel face.
                if (not face)
                    continue;
                idx = FT_Get_Char_Index(face.handle(), glyph);
                if (idx != 0)
                {
#ifndef NDEBUG
                    printf("Fallback font '%s' selected for glyph %x\n", face.handle()->family_name, glyph);
#endif // NDEBUG
                    return face.handle();
                }
            }
#ifndef NDEBUG
            fprintf(stderr, "Glyph %x has no appropriate font\n", glyph);
#endif // NDEBUG
            // Return the default face so that the renderer can consistently render missing glyph slots.
            return data->face.handle();
        }

        // Note: older versions of FreeType do not support SDF.  SDF is 'signed distance
        // field' bitmaps which allows us to more accurately AA (anti-alias) the font for
        // the given pixel size.
        //constexpr FT_Int32 rasterize_flags = FT_LOAD_RENDER | FT_LOAD_TARGET_(FT_RENDER_MODE_SDF);
        constexpr FT_Int32 rasterize_flags = FT_LOAD_RENDER | FT_LOAD_TARGET_(FT_RENDER_MODE_NORMAL);
        // Load the glyph without special SDF mode so that we can just measure the glyph size.
        constexpr FT_Int32 load_flags = FT_LOAD_RENDER | FT_LOAD_TARGET_(FT_RENDER_MODE_NORMAL);

        // After measuring a few times, I determined these values were roughly the constant overhead
        // that SDF rendering added to pad each glyph.  I'm willing to be proven wrong, in which case
        // we flip the 'load_flags' to 'FT_RENDER_MODE_SDF' and resolve the problem, but the normal
        // render mode allows for large unicode files to be loaded much faster due to only measure_text
        // being required to tokenize the file.
#if 0
        constexpr FT_UInt sdf_width_addition = 16;
        constexpr FT_UInt sdf_height_addition = 26;
#endif
        constexpr FT_UInt sdf_width_addition = 0;
        constexpr FT_UInt sdf_height_addition = 0;

        constexpr auto standard_reporter = [](std::string_view msg)
        {
            fprintf(stderr, "%s\n", msg.data());
        };

        template <typename Reporter>
        bool resize_font(FT_Face face, int size, Reporter&& reporter)
        {
            // Width == 0.  We don't want bold fonts.
#if 1 // DPI experiments.
            auto error = FT_Set_Pixel_Sizes(face, 0, size);
#else
            DPI dpi = get_platform_dpi();
#if 0
            float scaled_dpi = 92.f / rep(dpi);
            int scaled_size = static_cast<int>(size * scaled_dpi);
#else
            int scaled_size = size;
#endif
            auto error = FT_Set_Char_Size(face,
                                            0,
                                            scaled_size * 64,
                                            0,
                                            rep(dpi));
#endif
            if (error)
            {
                const char* log = FT_Error_String(error);
                auto msg = std::format("Failed to set font size: {}", log);
                reporter(msg);
                return false;
            }
            return true;
        }

        bool rasterize_cached_glyph(Atlas::Data* data, CachedFont* font, UnicodeGlyphInfo* info, UTF8::Codepoint glyph)
        {
            // Do not attempt to rasterize an invalid codepoint (what would we do anyway?).
            if (glyph == UTF8::invalid_codepoint)
                return false;
            auto* face = info->face;
            // If we could not identify a font face for this glyph, we're done.
            if (face == nullptr)
                return false;
            if (not resize_font(face, font->font_size, standard_reporter))
                return false;
            // Now we cache the resulting render.
            auto error = FT_Load_Char(face, static_cast<FT_ULong>(glyph), rasterize_flags);
            if (error != 0)
            {
                const char* log = FT_Error_String(error);
                fprintf(stderr, "Failed to load the glyph: %s\n", log);
                return false;
            }

            error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
            if (error != 0)
            {
                const char* log = FT_Error_String(error);
                fprintf(stderr, "Failed to render the glyph: %s\n", log);
                return false;
            }

            FT_UInt x = static_cast<FT_UInt>(info->info.tx * data->width);
            FT_UInt y = static_cast<FT_UInt>(info->info.ty * data->height);

            // This glyph cannot be rasterized.
            if (y + face->glyph->bitmap.rows > data->height)
                return false;

            info->info.ax = static_cast<float>(face->glyph->advance.x >> 6);
            info->info.ay = static_cast<float>(face->glyph->advance.y >> 6);
            info->info.bw = static_cast<float>(face->glyph->bitmap.width);
            info->info.bh = static_cast<float>(face->glyph->bitmap.rows);
            info->info.bl = static_cast<float>(face->glyph->bitmap_left);
            info->info.bt = static_cast<float>(face->glyph->bitmap_top);
            // Note: we do not need to update the texture coordinates because they're already
            // SDF-adjusted when we measured.

            Render::GlyphEntry entry{
                .offset_x = Render::GlyphOffsetX(x),
                .offset_y = Render::GlyphOffsetY(y),
                .width = Width(face->glyph->bitmap.width),
                .height = Height(face->glyph->bitmap.rows),
                .buffer = face->glyph->bitmap.buffer
            };
            Render::SceneRenderer::submit_glyph_data(data->texture, entry);

            // Fill in the info.
            info->rasterized = true;
            return info;
        }

        enum class Rasterize : bool { No, Yes };

        UnicodeGlyphInfo* request_cached_glyph(Atlas::Data* data, CachedFont* font, UTF8::Codepoint glyph, Rasterize rasterize)
        {
            // Do not attempt to rasterize an invalid codepoint (what would we do anyway?).
            if (glyph == UTF8::invalid_codepoint)
                return nullptr;

            auto [itr, inserted] = font->cached_glyphs_map.emplace(glyph, UnicodeGlyphInfo{ });
            if (not inserted)
            {
                if (not itr->second.rasterized
                    and is_yes(rasterize))
                {
                    auto* info = &itr->second;
                    if (info->failed_to_rasterize)
                        return nullptr;
                    info->failed_to_rasterize = not rasterize_cached_glyph(data, font, info, glyph);
                    if (info->failed_to_rasterize)
                        return nullptr;
                    return info;
                }
                return &itr->second;
            }
            auto* info = &itr->second;
            auto* face = identify_font_face_for_glyph(data, glyph);
            if (not resize_font(face, font->font_size, standard_reporter))
                return nullptr;
            // This ensures we append each successive bitmap image to the RHS of the last.
            FT_UInt x = data->next_x;
            FT_UInt y = data->next_y;
            // Now we cache the resulting glyph info.
            auto error = FT_Load_Char(face, static_cast<FT_ULong>(glyph), load_flags);
            if (error != 0)
            {
                const char* log = FT_Error_String(error);
                fprintf(stderr, "Failed to load the glyph: %s\n", log);
                return nullptr;
            }

            if (x + sdf_width_addition + face->glyph->bitmap.width > data->width)
            {
                y += data->cur_row_max_height;
                x = 0;
                data->cur_row_max_height = 0;
            }

            // Note: these are all updated by the time we go to rasterize.
            info->info.ax = static_cast<float>(face->glyph->advance.x >> 6);
            info->info.ay = static_cast<float>(face->glyph->advance.y >> 6);
            info->info.bw = static_cast<float>(face->glyph->bitmap.width);
            info->info.bh = static_cast<float>(face->glyph->bitmap.rows);
            info->info.bl = static_cast<float>(face->glyph->bitmap_left);
            info->info.bt = static_cast<float>(face->glyph->bitmap_top);

            info->info.tx = static_cast<float>(x) / static_cast<float>(data->width);
            info->info.ty = static_cast<float>(y) / static_cast<float>(data->height);

            // Note: because we're only measuring, we add the SDF width adjustments here.
            x += face->glyph->bitmap.width + sdf_width_addition;

            // Write back to the data starts.
            data->next_y = y;
            data->next_x = x;
            data->cur_row_max_height = std::max(data->cur_row_max_height, face->glyph->bitmap.rows + sdf_height_addition);

            // Tell the rasterization process which face to use.
            info->face = face;

            if (is_yes(rasterize))
            {
                info->failed_to_rasterize = not rasterize_cached_glyph(data, font, info, glyph);
                if (info->failed_to_rasterize)
                    return nullptr;
            }
            return info;
        }

        const Vec4f* default_color_filter(const Vec4f* default_color, const CustomContextColors*)
        {
            return default_color;
        }

        const Vec4f* whitespace_glyph_color_filter(const Vec4f*, const CustomContextColors* colors)
        {
            return &colors->whitespace;
        }

        const Vec4f* carriage_return_glyph_color_filter(const Vec4f*, const CustomContextColors* colors)
        {
            return &colors->carriage_return;
        }

        using ColorFilter = const Vec4f*(*)(const Vec4f*, const CustomContextColors*);

        struct GlyphExtractResult
        {
            const CharInfo& info;
            // Sometimes we need to adjust the x_advance based on config info such as tabstop.
            float x_advance;
            ColorFilter color_filter;
        };

        enum class RenderWhitespace : bool { No, Yes };

        GlyphExtractResult extract_glyph_info(Atlas::Data* data,
                                                CachedFont* font,
                                                Tabstop tabstop,
                                                UTF8::Codepoint glyph,
                                                Rasterize rasterize,
                                                RenderWhitespace render_whitespace)
        {
            ColorFilter filter = default_color_filter;
            if (glyph >= CharInfoCount)
            {
                // Sentinel value.
                if (glyph == UTF8::invalid_codepoint)
                {
                    glyph = '?';
                }
                else if (auto* info = request_cached_glyph(data, font, glyph, rasterize))
                {
                    return { .info = info->info, .x_advance = info->info.ax, .color_filter = filter };
                }
                // Either the glyph failed to rasterize or there's simply no mapping for it.
                else
                {
                    glyph = '?';
                }
            }

            if (glyph == ' ' and is_yes(render_whitespace))
            {
                glyph = rep(SpecialGlyph::Whitespace);
                filter = whitespace_glyph_color_filter;
            }

            if (glyph == '\r')
            {
                glyph = rep(SpecialGlyph::CarriageReturn);
                filter = carriage_return_glyph_color_filter;
            }

            if (glyph == '\t')
            {
                glyph = rep(SpecialGlyph::Tab);
                filter = whitespace_glyph_color_filter;
                // Compute the additional advance factor (based on the tab character glyph or the whitespace
                // glyph if render whitespace is off).
                if (not is_yes(render_whitespace))
                {
                    glyph = ' ';
                    filter = default_color_filter;
                }
                const float advance_x = font->infos[glyph].ax * static_cast<float>(rep(tabstop));
                return { .info = font->infos[glyph], .x_advance = advance_x, .color_filter = filter };
            }

            // If we still somehow have a control character, don't render it.
            if (glyph < ValidCharStart)
            {
                glyph = '?';
            }

            return { .info = font->infos[glyph], .x_advance = font->infos[glyph].ax, .color_filter = filter };
        }

        template <typename Reporter>
        bool populate_standard_glyphs(Atlas::Data* data, CachedFont* font, Reporter&& reporter)
        {
            // It is assumed on entry that the unicode map has not been populated and that the
            // texture has been cleared.

            // Note: (just like the wiki above) we skip the first 32 characters of the ASCII table
            // because they're simply control codes which we cannot render.
            auto* face = data->face.handle();

            // This ensures we append each successive bitmap image to the RHS of the last.
            int x = data->next_x;
            int y = data->next_y;
            int max_glyph_height_for_row = data->cur_row_max_height;
            // Set the font size for this population.
            if (not resize_font(face, font->font_size, reporter))
                return false;
            // Now we cache the resulting render.
            for (int i = ValidCharStart; i < CharInfoCount; ++i)
            {
                auto error = FT_Load_Char(face, i, rasterize_flags);
                if (error != 0)
                {
                    const char* log = FT_Error_String(error);
                    auto msg = std::format("Failed to load the glyph: {}", log);
                    reporter(msg);
                    return false;
                }

                error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
                if (error != 0)
                {
                    const char* log = FT_Error_String(error);
                    auto msg = std::format("Failed to render the glyph: {}", log);
                    reporter(msg);
                    return false;
                }

                if (x + face->glyph->bitmap.width > data->width)
                {
                    y += max_glyph_height_for_row;
                    x = 0;
                    max_glyph_height_for_row = 0;
                }

                font->infos[i].ax = static_cast<float>(face->glyph->advance.x >> 6);
                font->infos[i].ay = static_cast<float>(face->glyph->advance.y >> 6);
                font->infos[i].bw = static_cast<float>(face->glyph->bitmap.width);
                font->infos[i].bh = static_cast<float>(face->glyph->bitmap.rows);
                font->infos[i].bl = static_cast<float>(face->glyph->bitmap_left);
                font->infos[i].bt = static_cast<float>(face->glyph->bitmap_top);
                font->infos[i].tx = static_cast<float>(x) / static_cast<float>(data->width);
                font->infos[i].ty = static_cast<float>(y) / static_cast<float>(data->height);

                Render::GlyphEntry entry{
                    .offset_x = Render::GlyphOffsetX{ x },
                    .offset_y = Render::GlyphOffsetY{ y },
                    .width = Width(face->glyph->bitmap.width),
                    .height = Height(face->glyph->bitmap.rows),
                    .buffer = face->glyph->bitmap.buffer
                };
                Render::SceneRenderer::submit_glyph_data(data->texture, entry);

                x += face->glyph->bitmap.width;
                max_glyph_height_for_row = std::max(max_glyph_height_for_row, static_cast<int>(face->glyph->bitmap.rows));
            }

            // Special glyphs.
            for (const auto& e : special_glyph_map)
            {
                auto error = FT_Load_Char(face, e.glyph, rasterize_flags);
                if (error != 0)
                {
                    const char* log = FT_Error_String(error);
                    auto msg = std::format("Failed to load the glyph: {}", log);
                    reporter(msg);
                    return false;
                }

                error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
                if (error != 0)
                {
                    const char* log = FT_Error_String(error);
                    auto msg = std::format("Failed to render the glyph: {}", log);
                    reporter(msg);
                    return false;
                }

                if (x + face->glyph->bitmap.width > data->width)
                {
                    y += max_glyph_height_for_row;
                    x = 0;
                    max_glyph_height_for_row = 0;
                }

                font->infos[rep(e.index)].ax = static_cast<float>(face->glyph->advance.x >> 6);
                font->infos[rep(e.index)].ay = static_cast<float>(face->glyph->advance.y >> 6);
                font->infos[rep(e.index)].bw = static_cast<float>(face->glyph->bitmap.width);
                font->infos[rep(e.index)].bh = static_cast<float>(face->glyph->bitmap.rows);
                font->infos[rep(e.index)].bl = static_cast<float>(face->glyph->bitmap_left);
                font->infos[rep(e.index)].bt = static_cast<float>(face->glyph->bitmap_top);
                font->infos[rep(e.index)].tx = static_cast<float>(x) / static_cast<float>(data->width);
                font->infos[rep(e.index)].ty = static_cast<float>(y) / static_cast<float>(data->height);

                Render::GlyphEntry entry{
                    .offset_x = Render::GlyphOffsetX{ x },
                    .offset_y = Render::GlyphOffsetY{ y },
                    .width = Width(face->glyph->bitmap.width),
                    .height = Height(face->glyph->bitmap.rows),
                    .buffer = face->glyph->bitmap.buffer
                };
                Render::SceneRenderer::submit_glyph_data(data->texture, entry);

                x += face->glyph->bitmap.width;
                max_glyph_height_for_row = std::max(max_glyph_height_for_row, static_cast<int>(face->glyph->bitmap.rows));
            }

            // Start on the row just under the standard glyphs.
            data->unicode_row_start = y + max_glyph_height_for_row;
            data->next_y = data->unicode_row_start;
            data->next_x = 0;
            data->cur_row_max_height = 0;

            return true;
        }

        template <typename Reporter>
        bool try_set_font_size(Atlas::Data* data, int size, Reporter&& reporter)
        {
            auto [itr, inserted] = data->cached_fonts.emplace(size, CachedFont{ });
            data->selected_font = &itr->second;
            if (not inserted)
                return true;
            // Let's also clear the existing unicode cache.
            data->selected_font->font_size = size;
            data->selected_font->cached_glyphs_map.clear();
            return populate_standard_glyphs(data,
                                            data->selected_font,
                                            reporter);
        }

        constexpr Vec4f sentinel_color = hex_to_vec4f(0x00000000);
    } // namespace [anon]

    Atlas::Atlas():
        data{ new Data } { }

    Atlas::~Atlas() = default;

    RenderFontContext::RenderFontContext(Atlas* atlas, CachedFont* font):
        atlas{ atlas },
        font{ font },
        tabs{ 1 },
        colors{ .whitespace = sentinel_color, .carriage_return = sentinel_color },
        render_ws{ false }
    { }

    bool Atlas::init(std::string_view font_path)
    {
        FT_Library lib;
        auto error = FT_Init_FreeType(&lib);
        if (error != 0)
        {
            const char* log = FT_Error_String(error);
            fprintf(stderr, "Failed to initialize FreeType2 library: %s\n", log);
            return false;
        }
        data->library = FTLibraryHandle{ lib };

        FT_Face face{ };
        error = FT_New_Face(data->library.handle(), font_path.data(), 0, &face);
        if (error != 0)
        {
            const char* log = FT_Error_String(error);
            fprintf(stderr, "Failed to load font file '%s': %s\n", font_path.data(), log);
            return false;
        }
        data->face = FTFaceHandle{ face };

        constexpr FT_UInt pixel_size = Data::default_font_size;
        bool resize_success = resize_font(data->face.handle(), pixel_size, standard_reporter);
        return resize_success;
    }

    bool Atlas::populate_atlas()
    {
        // Set the width to the maximum width of the image.
        data->width = texture_width;
        data->height = texture_height;

        ScreenDimensions dim{ Width(data->width), Height(data->height) };
        data->texture = Render::SceneRenderer::create_glyph_texture(dim);

        return try_set_font_size(data.get(), Data::default_font_size, standard_reporter);
    }

    void Atlas::try_load_font_face(std::string_view path, Feed::MessageFeed* feed)
    {
        {
            FT_Face face{ };
            auto error = FT_New_Face(data->library.handle(), path.data(), 0, &face);
            if (error != 0)
            {
                const char* log = FT_Error_String(error);
                auto msg = std::format("Failed to load font file '{}': {}", path.data(), log);
                feed->queue_error(msg);
                return;
            }
            auto new_face = FTFaceHandle{ face };

            constexpr FT_UInt pixel_size = Data::default_font_size;
            bool resize_success = resize_font(new_face.handle(),
                                                pixel_size,
                                                [&](std::string_view view)
                                                {
                                                    feed->queue_error(view);
                                                });
            if (not resize_success)
                return;
            // At this point we can set the new font.
            data->face = std::move(new_face);
        }

        // Clear the data bounds for the texture.
        data->next_x = 0;
        data->next_y = 0;
        data->cur_row_max_height = 0;
        data->unicode_row_start = 0;
        // We must clear the existing texture.
        constexpr int buf_w = 64;
        constexpr int buf_h = 64;
        unsigned char arr[buf_w * buf_h] = { };
        static_assert((texture_width % buf_w) == 0);
        static_assert((texture_height % buf_h) == 0);
        for (int x = 0; x < (texture_width / buf_w); ++x)
        {
            for (int y = 0; y < (texture_height / buf_h); ++y)
            {
                Render::GlyphEntry entry{
                    .offset_x = Render::GlyphOffsetX{ x * buf_w },
                    .offset_y = Render::GlyphOffsetY{ y * buf_h },
                    .width = Width{ buf_w },
                    .height = Height{ buf_h },
                    .buffer = arr
                };
                Render::SceneRenderer::submit_glyph_data(data->texture, entry);
            }
        }

        // Clear out all cached fonts.
        data->cached_fonts.clear();
        // Populate a default font.
        const bool success = try_set_font_size(data.get(),
                                                Data::default_font_size,
                                                [&](std::string_view view)
                                                {
                                                    feed->queue_error(view);
                                                });
        if (success)
        {
            feed->queue_info("Font loaded.");
        }
    }

    std::string_view Atlas::font_family() const
    {
        return { data->face.handle()->family_name };
    }

    Vec2f RenderFontContext::render_text(Render::SceneRenderer* renderer,
                        std::string_view text,
                        const Vec2f& pos,
                        const Vec4f& color)
    {
        Vec2f new_pos = pos;
        UTF8::CodepointWalker walker{ text };
        while (not walker.exhausted())
        {
            auto cp = walker.next();
            const auto& [info, ax, filter] = extract_glyph_info(atlas->data.get(), font, tabs, cp, Rasterize::Yes, make_yes_no<RenderWhitespace>(render_ws));
            float x2 = new_pos.x + info.bl;
            float y2 = -new_pos.y - info.bt;
            float w = info.bw;
            float h = info.bh;

            new_pos.x += ax;
            new_pos.y += info.ay;

            auto* filtered_color = filter(&color, &colors);

            renderer->render_image(Vec2f(x2, -y2),
                                    Vec2f(w, -h),
                                    Vec2f(info.tx, info.ty),
                                    Vec2f((w) / static_cast<float>(atlas->data->width), (h) / static_cast<float>(atlas->data->height)),
                                    *filtered_color);
        }
        return new_pos;
    }

    Vec2f RenderFontContext::render_glyph(Render::SceneRenderer* renderer,
                        UTF8::Codepoint cp,
                        const Vec2f& pos,
                        const Vec4f& color)
    {
        Vec2f new_pos = pos;

        const auto& [info, ax, filter] = extract_glyph_info(atlas->data.get(), font, tabs, cp, Rasterize::Yes, make_yes_no<RenderWhitespace>(render_ws));
        float x2 = new_pos.x + info.bl;
        float y2 = -new_pos.y - info.bt;
        float w = info.bw;
        float h = info.bh;

        new_pos.x += ax;
        new_pos.y += info.ay;

        auto* filtered_color = filter(&color, &colors);

        renderer->render_image(Vec2f(x2, -y2),
                                Vec2f(w, -h),
                                Vec2f(info.tx, info.ty),
                                Vec2f((w) / static_cast<float>(atlas->data->width), (h) / static_cast<float>(atlas->data->height)),
                                *filtered_color);

        return new_pos;
    }

    Vec2f RenderFontContext::render_glyph_no_offsets(Render::SceneRenderer* renderer,
                    UTF8::Codepoint cp,
                    const Vec2f& pos,
                    const Vec4f& color)
    {
        Vec2f new_pos = pos;

        const auto& [info, ax, filter] = extract_glyph_info(atlas->data.get(), font, tabs, cp, Rasterize::Yes, make_yes_no<RenderWhitespace>(render_ws));
        float x2 = new_pos.x;
        float y2 = -new_pos.y;
        float w = info.bw;
        float h = info.bh;

        new_pos.x += ax;
        new_pos.y += info.ay;

        auto* filtered_color = filter(&color, &colors);

        renderer->render_image(Vec2f(x2, -y2),
                                Vec2f(w, -h),
                                Vec2f(info.tx, info.ty),
                                Vec2f((w) / static_cast<float>(atlas->data->width), (h) / static_cast<float>(atlas->data->height)),
                                *filtered_color);

        return new_pos;
    }

    Vec2f RenderFontContext::render_scaled_text(Render::SceneRenderer* renderer,
                        std::string_view text,
                        float scalar,
                        const Vec2f& pos,
                        const Vec4f& color)
    {
        Vec2f new_pos = pos;
        UTF8::CodepointWalker walker{ text };
        while (not walker.exhausted())
        {
            UTF8::Codepoint glyph_index = walker.next();
            const auto& [info, ax, filter] = extract_glyph_info(atlas->data.get(), font, tabs, glyph_index, Rasterize::Yes, make_yes_no<RenderWhitespace>(render_ws));
            float x2 = new_pos.x + info.bl * scalar;
            float y2 = -new_pos.y - info.bt * scalar;
            float w = info.bw * scalar;
            float h = info.bh * scalar;

            new_pos.x += ax * scalar;
            new_pos.y += info.ay * scalar;

            auto* filtered_color = filter(&color, &colors);

            renderer->render_image(Vec2f(x2, -y2),
                                    Vec2f(w, -h),
                                    Vec2f(info.tx, info.ty),
                                    Vec2f(info.bw / static_cast<float>(atlas->data->width), info.bh / static_cast<float>(atlas->data->height)),
                                    *filtered_color);
        }
        return new_pos;
    }

    void RenderFontContext::flush(Render::SceneRenderer* renderer)
    {
        atlas->bind_primary_texture();
        renderer->flush();
    }

    Vec2f RenderFontContext::measure_text(std::string_view text)
    {
        Vec2f size{};
        UTF8::CodepointWalker walker{ text };
        while (not walker.exhausted())
        {
            UTF8::Codepoint glyph_index = walker.next();
            const auto& [info, ax, filter] = extract_glyph_info(atlas->data.get(), font, tabs, glyph_index, Rasterize::No, make_yes_no<RenderWhitespace>(render_ws));
            size.x += ax;
            size.y += info.ay;
        }
        return size;
    }

    Vec2f RenderFontContext::measure_scaled_text(std::string_view text, float scalar)
    {
        Vec2f size{};
        UTF8::CodepointWalker walker{ text };
        while (not walker.exhausted())
        {
            UTF8::Codepoint glyph_index = walker.next();
            const auto& [info, ax, filter] = extract_glyph_info(atlas->data.get(), font, tabs, glyph_index, Rasterize::No, make_yes_no<RenderWhitespace>(render_ws));
            size.x += ax * scalar;
            size.y += info.ay * scalar;
        }
        return size;
    }

    Vec2f RenderFontContext::glyph_size(UTF8::Codepoint cp)
    {
        Vec2f size{};
        const auto& [info, ax, filter] = extract_glyph_info(atlas->data.get(), font, tabs, cp, Rasterize::No, make_yes_no<RenderWhitespace>(render_ws));
        size.x = info.bw;
        size.y = info.bh;
        return size;
    }

    size_t RenderFontContext::glyph_count_to_point(std::string_view text, float x_point)
    {
        size_t count = 0;
        float running_length = 0.f;
        UTF8::CodepointWalker walker{ text };
        while (not walker.exhausted())
        {
            UTF8::Codepoint glyph_index = walker.next();
            const auto& [info, ax, filter] = extract_glyph_info(atlas->data.get(), font, tabs, glyph_index, Rasterize::No, make_yes_no<RenderWhitespace>(render_ws));
            running_length += ax;
            if (running_length >= x_point)
            {
                // Let's do something nice.  If the point is > 50% of this glyph width, then we
                // will move the count forward.
                const float threshold = ax / 2.f;
                const float threshold_length = (running_length - info.ax) + threshold;
                if (threshold_length >= x_point)
                    return count;
            }
            ++count;
        }
        return count;
    }

    int RenderFontContext::current_font_size()
    {
        return font->font_size;
    }

    int RenderFontContext::current_font_line_height()
    {
        // The line height is always relative to the known default font size.
        constexpr double target_pct = 25. / Atlas::Data::default_font_size;
        const int padding = static_cast<int>(target_pct * font->font_size);
        return font->font_size + padding;
    }

    void RenderFontContext::tabstop(Tabstop ts)
    {
        tabs = ts;
    }

    void RenderFontContext::whitespace_color(const Vec4f& color)
    {
        colors.whitespace = color;
    }

    void RenderFontContext::carriage_return_color(const Vec4f& color)
    {
        colors.carriage_return = color;
    }

    void RenderFontContext::render_whitespace(bool b)
    {
        render_ws = b;
    }

    RenderFontContext Atlas::render_font_context(FontSize size)
    {
        try_set_font_size(data.get(), rep(size), standard_reporter);
        return { this, data->selected_font };
    }

    void Atlas::bind_primary_texture()
    {
        Render::SceneRenderer::bind_glyph_texture(data->texture);
    }
} // namespace Glyph