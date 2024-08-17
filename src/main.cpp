#include <cassert>

#include <format>
#include <string_view>

#include <SDL2/SDL.h>
#include <GL/glew.h>

#include "basic-scrollbox.h"
#include "basic-textbox.h"
#include "basic-window.h"
#include "choice.h"
#include "config.h"
#include "constants.h"
#include "examples.h"
#include "feed.h"
#include "glyph-cache.h"
#include "help.h"
#include "renderer.h"
#include "types.h"
#include "ui-common.h"
#include "util.h"
#include "vec.h"
#include "window-theming.h"

using namespace UI;

namespace
{
    enum class CommandMode
    {
        None,
        Help,
    };

    void ui_mouse_down(const SDL_Event& e, UIState* state)
    {
        if (e.button.button == SDL_BUTTON_RIGHT)
        {
            state->mouse |= MouseState::RDown;
        }
        else if (e.button.button == SDL_BUTTON_LEFT)
        {
            state->mouse |= MouseState::LDown;
        }
        else if (e.button.button == SDL_BUTTON_MIDDLE)
        {
            state->mouse |= MouseState::Middle;
        }
    }

    void ui_mouse_up(const SDL_Event& e, UIState* state)
    {
        if (e.button.button == SDL_BUTTON_RIGHT)
        {
            state->mouse = remove_flag(state->mouse, MouseState::RDown);
        }
        else if (e.button.button == SDL_BUTTON_LEFT)
        {
            state->mouse = remove_flag(state->mouse, MouseState::LDown);
        }
        else if (e.button.button == SDL_BUTTON_MIDDLE)
        {
            state->mouse = remove_flag(state->mouse, MouseState::Middle);
        }
    }

    void ui_keyup(const SDL_Event& e, UIState* state)
    {
        switch (e.key.keysym.sym)
        {
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
            {
                state->mods = remove_flag(state->mods, KeyMods::Shift);
            }
            break;
        case SDLK_LALT:
            {
                state->mods = remove_flag(state->mods, KeyMods::Alt);
            }
            break;
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            {
                state->mods = remove_flag(state->mods, KeyMods::Ctrl);
            }
            break;
        }
    }

    Vec2i ui_mouse_pos(const SDL_Event& e, const ScreenDimensions& screen)
    {
        return { e.button.x, rep(screen.height) - e.button.y };
    }

    Vec2i ui_mouse_wheel_pos(const SDL_Event& e, const ScreenDimensions& screen)
    {
        return { e.wheel.mouseX, rep(screen.height) - e.wheel.mouseY };
    }

    void apply_multipass_postprocessing_crt(Render::SceneRenderer* renderer, const ScreenDimensions& screen, const Config::SystemEffects& system_effects)
    {
        // We're going to start a multi-pass shader.
        // Take the texture at FB0 and linearize it.
        renderer->bind_framebuffer(Render::Framebuffer::Scratch1);
        renderer->set_shader(Render::FragShader::CRTEasymodeLinearize);
        renderer->render_framebuffer(screen, Render::Framebuffer::Default);

        // Blur-horiz
        renderer->bind_framebuffer(Render::Framebuffer::Scratch2);
        renderer->custom_float_value1(0.25f); // GLOW_FALLOFF.
        renderer->custom_float_value2(4.0); // TAPS.
        renderer->set_shader(Render::FragShader::CRTEasymodeBlurHoriz);
        renderer->render_framebuffer(screen, Render::Framebuffer::Scratch1);

        // Blur-vert.
        renderer->bind_framebuffer(Render::Framebuffer::Scratch1);
        renderer->custom_float_value1(0.25f); // GLOW_FALLOFF.
        renderer->custom_float_value2(4.0); // TAPS.
        renderer->set_shader(Render::FragShader::CRTEasymodeBlurVert);
        renderer->render_framebuffer(screen, Render::Framebuffer::Scratch2);

        // Threshold.
        // This shader needs access to the original input texture for diffing.
        renderer->bind_framebuffer(Render::Framebuffer::Scratch2);
        renderer->enable_prev_pass_texture(Render::Framebuffer::Default);
        renderer->set_shader(Render::FragShader::CRTEasymodeThresh);
        renderer->render_framebuffer(screen, Render::Framebuffer::Scratch1);

        // Halation.
        // This shader needs access to the original input texture for blending.
        renderer->bind_framebuffer(Render::Framebuffer::Scratch1);
        renderer->enable_prev_pass_texture(Render::Framebuffer::Default);
        renderer->set_shader(Render::FragShader::CRTEasymodeHalation);
        renderer->render_framebuffer(screen, Render::Framebuffer::Scratch2);

        // Finally, unbind and set the shader back to regular image.
        // If screen warping is enabled, we're going to reuse FB0 to render the warp and finally render that.
        if (system_effects.screen_warp)
        {
            renderer->bind_framebuffer(Render::Framebuffer::Default);
            renderer->set_shader(Render::FragShader::CRTWarp);
            renderer->render_framebuffer(screen, Render::Framebuffer::Scratch1);

            renderer->unbind_framebuffer();
            renderer->set_shader(Render::FragShader::Image);
            renderer->render_framebuffer(screen, Render::Framebuffer::Default);
        }
        else
        {
            renderer->unbind_framebuffer();
            renderer->set_shader(Render::FragShader::Image);
            renderer->render_framebuffer(screen, Render::Framebuffer::Scratch1);
        }
    }

    void apply_postprocessing_crt(Render::SceneRenderer* renderer, const ScreenDimensions& screen, const Config::SystemEffects& system_effects)
    {
        if (system_effects.multipass_crt)
        {
            apply_multipass_postprocessing_crt(renderer, screen, system_effects);
            return;
        }

        if (system_effects.screen_warp)
        {
            // We're going to swap to a new frame buffer so we can add a second pass to
            // warp it.
            renderer->bind_framebuffer(Render::Framebuffer::Scratch1);
            renderer->set_shader(Render::FragShader::CRTEasymode);
            renderer->render_framebuffer(screen, Render::Framebuffer::Default);

            // Now warp it and render it to the default render buffer.
            renderer->unbind_framebuffer();
            renderer->set_shader(Render::FragShader::CRTWarp);
            renderer->render_framebuffer(screen, Render::Framebuffer::Scratch1);
        }
        else
        {
            renderer->set_shader(Render::FragShader::CRTEasymode);
            renderer->render_framebuffer(screen, Render::Framebuffer::Default);
        }
    }

    void apply_framebuffer(Render::SceneRenderer* renderer, const ScreenDimensions& screen, const Config::SystemEffects& system_effects)
    {
        renderer->unbind_framebuffer();
        renderer->set_shader(Render::VertShader::NoTransform);
        if (system_effects.postprocessing_enabled and system_effects.crt_mode)
        {
            apply_postprocessing_crt(renderer, screen, system_effects);
            return;
        }
        // Simple render of the primary framebuffer 0.
        renderer->set_shader(Render::FragShader::Image);
        renderer->render_framebuffer(screen, Render::Framebuffer::Default);
    }

    enum class CursorStyle
    {
        Default,
        IBeam,
        Select,
        UpDownArrow,
        LeftRightArrow,
        SouthEastArrow,  // Arrow pointing South East.
        SouthWestArrow,  // Arrow pointing South West.
        Count
    };

    class MouseCursorManager
    {
    public:
        void init()
        {
            for (auto x = CursorStyle{}; x != CursorStyle::Count; x = extend(x))
            {
                switch (x)
                {
                case CursorStyle::Default:
                    cursors[rep(x)] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
                    break;
                case CursorStyle::IBeam:
                    cursors[rep(x)] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
                    break;
                case CursorStyle::Select:
                    cursors[rep(x)] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
                    break;
                case CursorStyle::UpDownArrow:
                    cursors[rep(x)] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
                    break;
                case CursorStyle::LeftRightArrow:
                    cursors[rep(x)] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
                    break;
                case CursorStyle::SouthEastArrow:
                    cursors[rep(x)] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
                    break;
                case CursorStyle::SouthWestArrow:
                    cursors[rep(x)] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
                    break;
                case CursorStyle::Count:
                    break;
                default:
                    assert(not "fix cursor styles");
                }
            }
        }

        void select_cursor(CursorStyle style)
        {
            SDL_SetCursor(cursors[rep(style)]);
        }

    private:
        // I don't really care about freeing these...
        SDL_Cursor* cursors[count_of<CursorStyle>];
    };
} // namespace [anon]

int main(int argc, char** argv)
{
    (void)argc,argv;
    // This needs to be done before we build the primary render window.
    setup_platform_dpi();

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        fprintf(stderr, "ERROR: Could not initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window =
        SDL_CreateWindow("basic-ui-template",
                         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         rep(Constants::screen.width), rep(Constants::screen.height),
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);

    if (window == nullptr)
    {
        fprintf(stderr, "ERROR: Could not create SDL window: %s\n", SDL_GetError());
        return 1;
    }

    auto window_id = SDL_GetWindowID(window);

    // Directly request OpenGL 3.2 so we can use things like RenderDoc.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

    if (SDL_GL_CreateContext(window) == nullptr)
    {
        fprintf(stderr, "ERROR: Could not create OpenGL context: %s\n", SDL_GetError());
        return 1;
    }

    if (SDL_Init(SDL_INIT_AUDIO) < 0)
    {
        fprintf(stderr, "ERROR: Could not initialize SDL audio: %s\nAudio functionality may not work\n", SDL_GetError());
    }

    if (glewInit() != GLEW_OK)
    {
        fprintf(stderr, "ERROR: Could not initialize GLEW!");
        return 1;
    }

#ifndef NDEBUG
    // Now that GLEW is setup.  We can query for the OpenGL version.
    {
        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        printf("OpenGL version %d.%d\n", major, minor);
    }
#endif // NDEBUG

    // Initial window size.
    int w;
    int h;
    SDL_GetWindowSize(window, &w, &h);
    ScreenDimensions screen = { Width{ w }, Height{ h } };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    Render::SceneRenderer renderer;
    Glyph::Atlas atlas;
    Feed::MessageFeed message_feed;
    Help::Help help;
    Choice::Chooser chooser;

    // Examples to use.
    Examples::Intro ex_intro;
    Examples::DragNSnap ex_dragnsnap;

    // Box group.
    UI::Widgets::ScrollBox scroll_box;
    UI::Widgets::BasicTextbox text_box;
    UI::Widgets::BasicWindow scroll_window;
    bool scroll_window_closed = false;

    // Note: The config needs to be loaded before we load the file so that when the editor goes to build the model
    // it will have the correct colors, fonts, etc.
    const auto default_cfg_dir = default_config_directory();
    if (not file_exists(default_cfg_dir))
    {
        message_feed.queue_info("No existing config, creating...");

        // We're going to generate the default asset path, which is the base path of the executable.
        auto* exe_path = SDL_GetBasePath();
        auto system_core_cfg = Config::system_core();
        system_core_cfg.base_asset_path = exe_path;
        SDL_free(exe_path);
        Config::update(system_core_cfg);

        if (Config::save_config(default_cfg_dir, &message_feed))
        {
            auto msg = std::format("Config created at: {}", default_cfg_dir);
            message_feed.queue_info(msg);
        }
    }
    else if (Config::load_config(default_cfg_dir, &message_feed))
    {
        auto msg = std::format("Config loaded at: {}", default_cfg_dir);
        message_feed.queue_info(msg);
    }

    std::string asset_path;
    if (not dir_exists(Config::system_core().base_asset_path))
    {
        auto system_core_cfg = Config::system_core();
        auto* exe_path = SDL_GetBasePath();
        auto msg = std::format("Asset path of '{}' is invalid.  Defaulting to '{}'.", system_core_cfg.base_asset_path, exe_path);
        message_feed.queue_warning(msg);
        system_core_cfg.base_asset_path = exe_path;
        SDL_free(exe_path);
        Config::update(system_core_cfg);
    }
    asset_path = Config::system_core().base_asset_path;

    // Close your eyes for a second...
    // We need to set the working directory to the executable dir so we can properly get assets.
    // Don't worry!  We set it back!
    auto current_dir = working_dir();
    set_working_dir(asset_path.c_str());

    // Setup the platform window.
    set_platform_window(OpaqueWindow{ window });

    Theme::init(&message_feed);
    Theme::apply_boarder_color(get_platform_window(), &message_feed);

    if (not atlas.init(Config::system_fonts().current_font))
        return 1;

    if (not Render::SceneRenderer::init(screen))
        return 1;

    // Populate initial resolutions.
    renderer.resolution(Vec2f(static_cast<float>(rep(Constants::screen.width)),
                                static_cast<float>(rep(Constants::screen.height))));

    // Now we can populate the atlas since the renderer set up the graphics context.
    if (not atlas.populate_atlas())
        return 1;

    // This allows the cursor to be moved when the window is not focused and then regains focus from a click onto the
    // canvas.
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    // Ensure that vsync is enabled.
    // We try adaptive vsync first (-1) then fallback to regular vsync (1).
    if (SDL_GL_SetSwapInterval(-1) != 0)
    {
        SDL_GL_SetSwapInterval(1);
    }

    // We're done!
    set_working_dir(current_dir.c_str());

    // Main loop state.
    MouseCursorManager cursor_manager;
    UIState ui_state;
    float fps = 0.f;
    Uint32 last_update = 0;
    Uint32 last_fps_update = 0;
    std::string fps_text;
    Config::SystemEffects system_effects_state = Config::system_effects();
    CommandMode cmd_mode = CommandMode::None;
    bool quit = false;

    message_feed.queue_warning("Press 'F1' for help.");

    cursor_manager.init();

    // Init the Drag'n Snap viewport.
    auto drag_n_snap_viewport = Render::RenderViewport::basic(screen);
    drag_n_snap_viewport.height = Height{ 100 };
    drag_n_snap_viewport.width = Width{ rep(screen.width) - 20 };
    drag_n_snap_viewport.offset_x = Render::ViewportOffsetX{ 10 };

    // Setup box group state.
    auto scroll_window_viewport = Render::RenderViewport::basic(screen);
    scroll_window_viewport.width = Width(rep(screen.width) * 0.4);
    scroll_window_viewport.height = Height(rep(screen.height) * 0.2);
    scroll_window_viewport.offset_x = Render::ViewportOffsetX(rep(screen.width) - rep(scroll_window_viewport.width) - 25.f);
    scroll_window_viewport.offset_y = Render::ViewportOffsetY(rep(screen.height) - rep(scroll_window_viewport.height) - 25.f);

    // Store some text.
    {
        constexpr std::string_view txt = R"(#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
  using namespace std;
  vector<int> v{0, 0, 3, -1,
                    2, 4, 5, 0, 7};
  stable_partition(v.begin(),
                    v.end(),
                    [](int n)
                    {
                      return n > 0;
                    });
  for (int n : v)
      cout << n << ' ';
  cout << '\n';
})";
        text_box.text(txt);
        auto text_box_content_size = text_box.content_size(&atlas);
        scroll_box.content_size(text_box_content_size);
        scroll_window.title("Scrollbar Example");
    }

    // At this point we can process argv.

    while (not quit)
    {
        SDL_Event e{ 0 };

        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
            case SDL_QUIT:
                quit = true;
                break;
            case SDL_WINDOWEVENT:
            {
                if (e.window.windowID == window_id)
                {
                    switch (e.window.event)
                    {
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                        {
                            w = e.window.data1;
                            h = e.window.data2;

                            // Ensure pixels snap to an even number.
                            if ((w & 1) == 1)
                                w += 1;
                            if ((h & 1) == 1)
                                h += 1;

                            screen = { Width{ w }, Height{ h } };
                            glViewport(0, 0, w, h);

                            // Update the renderers.
                            renderer.resolution(Vec2f(static_cast<float>(w), static_cast<float>(h)));
                            Render::SceneRenderer::screen_resize(screen);

                            // Update Drag'n Snap viewport.
                            drag_n_snap_viewport.height = Height{ 100 };
                            drag_n_snap_viewport.width = Width{ rep(screen.width) - 20 };
                            drag_n_snap_viewport.offset_x = Render::ViewportOffsetX{ 10 };
                        }
                        break;
                    case SDL_WINDOWEVENT_HIDDEN:
                        ui_state.special |= SpecialModes::SuspendRendering;
                        break;
                    case SDL_WINDOWEVENT_SHOWN:
                        ui_state.special = remove_flag(ui_state.special, SpecialModes::SuspendRendering);
                        break;
                    case SDL_WINDOWEVENT_MINIMIZED:
                        ui_state.special |= SpecialModes::SuspendRendering;
                        break;
                    case SDL_WINDOWEVENT_FOCUS_LOST:
                        ui_state.special |= SpecialModes::SuspendRendering;
                        break;
                    case SDL_WINDOWEVENT_FOCUS_GAINED:
                        ui_state.special = remove_flag(ui_state.special, SpecialModes::SuspendRendering);
                        break;
                    }
                }
            }
            break;
            // Mouse input.
            case SDL_MOUSEWHEEL:
                {
                    Vec2i current_mouse = ui_mouse_wheel_pos(e, screen);
                    auto scroll_viewport = scroll_window.content_viewport(scroll_window_viewport);
                    if (e.wheel.preciseY > 0)
                    {
                        scroll_box.scroll_up(5.f, current_mouse, scroll_viewport);
                    }
                    else
                    {
                        scroll_box.scroll_down(5.f, current_mouse, scroll_viewport);
                    }
                    text_box.offset(scroll_box.position());
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                {
                    ui_mouse_down(e, &ui_state);

                    Vec2i current_mouse = ui_mouse_pos(e, screen);
                    // Process mouse here.
                    ex_dragnsnap.mouse_down(ui_state, current_mouse);
                    text_box.offset(scroll_box.position());
                    {
                        auto result = scroll_window.mouse_down(ui_state, current_mouse, scroll_window_viewport);
                        if (result.area == UI::Widgets::WindowMouseArea::Content)
                        {
                            auto scroll_viewport = scroll_window.content_viewport(scroll_window_viewport);
                            scroll_box.mouse_down(ui_state, current_mouse, scroll_viewport);
                        }
                    }
                }
                break;
            case SDL_MOUSEBUTTONUP:
                {
                    ui_mouse_up(e, &ui_state);
                    Vec2i current_mouse = ui_mouse_pos(e, screen);
                    auto scroll_viewport = scroll_window.content_viewport(scroll_window_viewport);
                    // Process mouse here.
                    ex_dragnsnap.mouse_up(ui_state, current_mouse);
                    scroll_box.mouse_up(ui_state, current_mouse, scroll_viewport);
                    text_box.offset(scroll_box.position());
                    {
                        auto result = scroll_window.mouse_up(ui_state, current_mouse, scroll_window_viewport);
                        if (result.close)
                        {
                            message_feed.queue_info("Close window.");
                            scroll_window_closed = true;
                        }
                    }
                }
                break;
            case SDL_MOUSEMOTION:
                {
                    Vec2i current_mouse = ui_mouse_pos(e, screen);
                    auto scroll_viewport = scroll_window.content_viewport(scroll_window_viewport);
                    // Process mouse here.
                    ex_dragnsnap.mouse_move(ui_state, current_mouse, drag_n_snap_viewport);
                    scroll_box.mouse_move(ui_state, current_mouse, scroll_viewport);
                    text_box.offset(scroll_box.position());
                    {
                        auto result = scroll_window.mouse_move(ui_state, current_mouse, scroll_window_viewport);
                        if (result.dragging)
                        {
                            scroll_window_viewport.offset_x = Render::ViewportOffsetX{ result.move_offset.x };
                            scroll_window_viewport.offset_y = Render::ViewportOffsetY{ result.move_offset.y };
                        }

                        if (result.resizing)
                        {
                            scroll_window_viewport = result.resize_viewport;
                        }

                        switch (result.area)
                        {
                        case UI::Widgets::WindowMouseArea::HorizBoarder:
                            cursor_manager.select_cursor(CursorStyle::UpDownArrow);
                            break;
                        case UI::Widgets::WindowMouseArea::VertBoarder:
                            cursor_manager.select_cursor(CursorStyle::LeftRightArrow);
                            break;
                        case UI::Widgets::WindowMouseArea::SECorner:
                            cursor_manager.select_cursor(CursorStyle::SouthEastArrow);
                            break;
                        case UI::Widgets::WindowMouseArea::SWCorner:
                            cursor_manager.select_cursor(CursorStyle::SouthWestArrow);
                            break;
                        default:
                            cursor_manager.select_cursor(CursorStyle::Default);
                            break;
                        }
                    }
                }
                break;
            case SDL_KEYUP:
                {
                    ui_keyup(e, &ui_state);
                }
                break;
            case SDL_KEYDOWN:
                {
                    switch (e.key.keysym.sym)
                    {
                    case SDLK_LSHIFT:
                    case SDLK_RSHIFT:
                        {
                            ui_state.mods |= KeyMods::Shift;
                        }
                        break;
                    case SDLK_LALT:
                        {
                            ui_state.mods |= KeyMods::Alt;
                        }
                        break;
                    case SDLK_LCTRL:
                    case SDLK_RCTRL:
                        {
                            ui_state.mods |= KeyMods::Ctrl;
                        }
                        break;
                    case SDLK_w:
                        quit = true;
                        break;
                    case SDLK_ESCAPE:
                        cmd_mode = CommandMode::None;
                        break;
                    case SDLK_F11:
                        if (implies(ui_state.mods, KeyMods::Ctrl))
                        {
                            ui_state.special = toggle(ui_state.special, SpecialModes::ShowGlyphs);
                        }
                        break;
                    case SDLK_F9:
                        {
                            std::string old_font = Config::system_fonts().current_font;
                            if (Config::load_config(default_cfg_dir, &message_feed))
                            {
                                system_effects_state = Config::system_effects();

                                // Update components.
                                Theme::apply_boarder_color(get_platform_window(), &message_feed);

                                // Update font if necessary.
                                if (old_font != Config::system_fonts().current_font)
                                {
                                    atlas.try_load_font_face(Config::system_fonts().current_font, &message_feed);
                                }

                                message_feed.queue_info("Config reloaded.");
                            }
                        }
                        break;
                    case SDLK_F6:
                        message_feed.queue_info("Reloading shaders...");
                        Render::SceneRenderer::reload_shaders(asset_path, &message_feed);
                        break;
                    case SDLK_F5:
                        message_feed.queue_info("Toggle show FPS.");
                        ui_state.special = toggle(ui_state.special, SpecialModes::ShowFPS);
                        break;
                    case SDLK_F1:
                        if (cmd_mode == CommandMode::None)
                        {
                            cmd_mode = CommandMode::Help;
                        }
                        else if (cmd_mode == CommandMode::Help)
                        {
                            cmd_mode = CommandMode::None;
                        }
                        break;

                    default:
                        break;
                    }
                }
                break;
            case SDL_TEXTINPUT:
                {
                    std::string_view text = e.text.text;
                    for (char c : text)
                    {
                        switch (cmd_mode)
                        {
                        case CommandMode::None:
                            message_feed.queue_info({ &c, 1 });
                            break;
                        case CommandMode::Help:
                            break;
                        }
                    }
                }
                break;
            }
        }

        if (not implies(ui_state.special, SpecialModes::SuspendRendering))
        {
            const Uint32 start = rep(ticks_since_app_start());

            // Setup the primary framebuffer.
            renderer.bind_framebuffer(Render::Framebuffer::Default);
            glEnable(GL_BLEND);
            renderer.apply_blending_mode(Render::BlendingMode::Default);

            const Vec4f bg = Config::system_colors().background;
            renderer.reset_current_buffer(bg);

            // Primary render.
            // We will wrap 'time' for the renderer so that we do not hit floating point limitations.
            // Wrap this at 60 minutes (or 60m * 60s * 1000ms).
            constexpr Uint32 wrap_time = 60 * 60 * 1000;
            const float wrapped_time = static_cast<float>(start % wrap_time) / 1000.f;
            renderer.update_time(wrapped_time);

            ex_intro.render(&renderer, &atlas, screen);

            // Put Drag'n snap on the bottom.
            {
                auto vp = renderer.create_scissor_viewport(screen);
                vp.apply_viewport(drag_n_snap_viewport);
                ex_dragnsnap.render(&renderer, &atlas, drag_n_snap_viewport);
            }

            // Scroll box.
            if (not scroll_window_closed)
            {
                auto vp = renderer.create_scissor_viewport(screen);
                // Primary window first.
                vp.apply_viewport(scroll_window_viewport);
                scroll_window.render(&renderer, &atlas, scroll_window_viewport);

                // Then scroll container.
                auto scroll_viewport = scroll_window.content_viewport(scroll_window_viewport);
                vp.reset_viewport();
                vp.apply_viewport(scroll_viewport);
                scroll_box.render(&renderer, scroll_viewport);

                // Finally content.
                auto viewport_content = scroll_box.content_viewport(scroll_viewport);
                vp.reset_viewport();
                vp.apply_viewport(viewport_content);
                text_box.render(&renderer, &atlas, viewport_content);
            }

            switch (cmd_mode)
            {
            case CommandMode::None:
                break;
            case CommandMode::Help:
                help.render(&renderer, &atlas, screen);
                break;
            }

            message_feed.render_queue(&renderer, &atlas, screen);

            // Draw some FPS.
            if (implies(ui_state.special, SpecialModes::ShowFPS))
            {
                const bool update_fps_txt = (last_update - last_fps_update) > 250;
                if (update_fps_txt)
                {
                    fps_text = std::format("FPS: {:.2f}", fps);
                    last_fps_update = last_update;
                }
                constexpr Vec4f color = hex_to_vec4f(0xC88837FF);
                renderer.set_shader(Render::VertShader::OneOneTransform);
                renderer.set_shader(Render::FragShader::Text);
                constexpr auto fps_font_size = Glyph::FontSize{ 32 };
                auto fps_font_ctx = atlas.render_font_context(fps_font_size);
                // Put it in the top right corner.
                fps_font_ctx.render_text(&renderer,
                    fps_text,
                    { 10.f,
                        rep(screen.height) - rep(fps_font_size) + 0.f },
                    color);
                fps_font_ctx.flush(&renderer);
            }

            if (implies(ui_state.special, SpecialModes::ShowGlyphs))
            {
                renderer.set_shader(Render::VertShader::NoTransform);
                renderer.set_shader(Render::FragShader::Image);
                auto width = rep(screen.width);
                auto height = rep(screen.height);
                renderer.render_image(Vec2f(-width + 0.f, 0.f),
                                        Vec2f(width * 2.f, -height * 2.f),
                                        Vec2f(0.f, 0.f),
                                        Vec2f(1.f, 1.f),
                                    hex_to_vec4f(0xFFFFFFFF));
                renderer.flush();
            }

            // Before we can apply the frame buffer, we must first disable image blending otherwise we will see
            // odd artifacts from blending the current frame buffer with the image on the default frame buffer.
            glDisable(GL_BLEND);
            // Finished rendering.  Unbind the frame buffer and blit it for displaying.
            apply_framebuffer(&renderer, screen, system_effects_state);

            const Uint32 turnover_ticks = rep(ticks_since_app_start());

            fps = 1.f / ((turnover_ticks - last_update) / 1000.f);
            last_update = start;

            // Swap the buffer.
            SDL_GL_SwapWindow(window);
        }
        else
        {
            // Provide some delay so the CPU does not poll events as fast as possible.
            constexpr auto target_fps_delta_ms = 16;
            SDL_Delay(target_fps_delta_ms);
        }
    }
    SDL_Quit();
    return 0;
}