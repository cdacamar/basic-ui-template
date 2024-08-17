#include "svg.h"

#include <cassert>

#include <format>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable: 4456) // declaration of 'name' hides previous local declaration
#pragma warning(disable: 4244) // '=': conversion from 'double' to 'float', possible loss of data
#pragma warning(disable: 4457) // declaration of 'path' hides function parameter
#pragma warning(disable: 4702) // unreachable code
#include "nanosvg.h"
#include "nanosvgrast.h"
#pragma warning(pop)

#include "feed.h"
#include "renderer.h"

namespace SVG
{
    namespace
    {
        struct RasterCleanup
        {
            void operator()(NSVGrasterizer* raster)
            {
                if (raster != nullptr)
                {
                    nsvgDeleteRasterizer(raster);
                }
            }
        };

        using RasterPtr = std::unique_ptr<NSVGrasterizer, RasterCleanup>;

        struct ImageCleanup
        {
            void operator()(NSVGimage* image)
            {
                if (image != nullptr)
                {
                    nsvgDelete(image);
                }
            }
        };

        using ImagePtr = std::unique_ptr<NSVGimage, ImageCleanup>;

        constinit RasterPtr rasterizer;
    } // namespace [anon]

    LoadSVGResult load_svg(std::string_view svg_path, Feed::MessageFeed* feed)
    {
        // Load.
        // Annoyingly, this only accepts a null-terminated string, but we should only be provided null-terminated strings.
        assert(svg_path.size() == std::strlen(svg_path.data()));
        // Hardcode the DPI for now.
        constexpr int DPI = 96;
        ImagePtr image = ImagePtr{ nsvgParseFromFile(svg_path.data(), "px", DPI) };
        if (not image)
        {
            auto msg = std::format("Unable to load SVG: '{}'", svg_path);
            feed->queue_error(msg);
            return { .tex = Render::BasicTexture::Invalid };
        }

        if (not rasterizer)
        {
            rasterizer = RasterPtr{ nsvgCreateRasterizer() };
        }

        if (not rasterizer)
        {
            feed->queue_error("Unable to create rasterizer");
            return { .tex = Render::BasicTexture::Invalid };
        }
        int w = static_cast<int>(image->width);
        int h = static_cast<int>(image->height);
        float scale = 1.f;
        if (h != 32)
        {
            // Find the scale for which this will be 32.
            scale = 32.f / h;
            h = 32;
            w = static_cast<int>(scale * w);
        }
        std::vector<std::uint8_t> img(w*h*4);
        nsvgRasterize(rasterizer.get(), image.get(), 0, 0, scale, img.data(), w, h, w * 4);
        ScreenDimensions size{ .width = Width{ w }, .height = Height{ h } };
        auto tex = Render::SceneRenderer::create_basic_texture(size);
        Render::BasicTextureEntry entry{
            .offset_x = {},
            .offset_y = {},
            .width = size.width,
            .height = size.height,
            .buffer = img.data()
        };
        Render::SceneRenderer::submit_basic_texture_data(tex, entry);
        return { .tex = tex, .size = size };
    }
} // namespace SVG