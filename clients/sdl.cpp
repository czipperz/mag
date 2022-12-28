#include "sdl.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_syswm.h>
#include <SDL_ttf.h>
#include <inttypes.h>
#include <math.h>
#include <Tracy.hpp>
#include <command_macros.hpp>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/file.hpp>
#include <cz/heap.hpp>
#include <cz/path.hpp>
#include <cz/str.hpp>
#include <cz/working_directory.hpp>
#include "basic/copy_commands.hpp"
#include "cell.hpp"
#include "client.hpp"
#include "command_macros.hpp"
#include "custom/config.hpp"
#include "file.hpp"
#include "movement.hpp"
#include "program_info.hpp"
#include "render.hpp"
#include "server.hpp"
#include "window_cache.hpp"

#ifdef _WIN32
#include <shellscalingapi.h>
#include <windows.h>
#include "res/resources.h"
#endif

using namespace mag::render;

namespace mag {
namespace client {
namespace sdl {

static SDL_Color make_color(uint32_t* colors, Face_Color fc) {
    SDL_Color color;
    if (fc.is_themed) {
        uint32_t c = colors[fc.x.theme_index];

        color.r = (c & 0xFF0000) >> 16;
        color.g = (c & 0x00FF00) >> 8;
        color.b = (c & 0x0000FF);
    } else {
        color.r = fc.x.color.r;
        color.g = fc.x.color.g;
        color.b = fc.x.color.b;
    }
    color.a = 0xFF;
    return color;
}

static SDL_Surface* render_character(TTF_Font* font,
                                     char character,
                                     uint8_t underscore,
                                     uint8_t bold,
                                     uint8_t italics,
                                     SDL_Color fgc) {
    ZoneScoped;

    char buffer[2] = {};
    buffer[0] = character;

    {
        ZoneScopedN("TTF_SetFontStyle");
        int style = 0;
        if (underscore) {
            style |= TTF_STYLE_UNDERLINE;
        }
        if (bold) {
            style |= TTF_STYLE_BOLD;
        }
        if (italics) {
            style |= TTF_STYLE_ITALIC;
        }
        TTF_SetFontStyle(font, style);
    }

    SDL_Surface* rendered_char;
    {
        ZoneScopedN("TTF_RenderText_Blended");
        rendered_char = TTF_RenderText_Blended(font, buffer, fgc);
    }
    return rendered_char;
}

static SDL_Surface* lookup_surface_cache(SDL_Surface**** surface_cache,
                                         TTF_Font* font,
                                         uint8_t underscore,
                                         uint8_t bold,
                                         uint8_t italics,
                                         size_t theme_index,
                                         SDL_Color color,
                                         unsigned char character) {
    ZoneScoped;

    if (!surface_cache[theme_index]) {
        surface_cache[theme_index] = cz::heap_allocator().alloc_zeroed<SDL_Surface**>(8);
    }
    SDL_Surface*** surface_cache_1 = surface_cache[theme_index];

    uint8_t flags = underscore | (bold << 1) | (italics << 2);
    if (!surface_cache_1[flags]) {
        surface_cache_1[flags] =
            cz::heap_allocator().alloc_zeroed<SDL_Surface*>((size_t)UCHAR_MAX + 1);
    }
    SDL_Surface** surface_cache_2 = surface_cache_1[flags];

    if (!surface_cache_2[character]) {
        surface_cache_2[character] =
            render_character(font, character, underscore, bold, italics, color);
    }
    return surface_cache_2[character];
}

void drop_surface_cache(SDL_Surface**** surface_cache) {
    ZoneScoped;

    for (size_t theme_index = 0; theme_index < 256; ++theme_index) {
        if (!surface_cache[theme_index]) {
            continue;
        }

        for (uint8_t flags = 0; flags < 8; ++flags) {
            if (!surface_cache[theme_index][flags]) {
                continue;
            }

            for (size_t character = 0; character <= UCHAR_MAX; ++character) {
                if (!surface_cache[theme_index][flags][character]) {
                    continue;
                }

                SDL_FreeSurface(surface_cache[theme_index][flags][character]);
            }

            cz::heap_allocator().dealloc(surface_cache[theme_index][flags], (size_t)UCHAR_MAX + 1);
        }

        cz::heap_allocator().dealloc(surface_cache[theme_index], 8);
    }
}

static Face_Color get_face_color_or(Face_Color fc, int16_t deflt) {
    // int16_t max_colors = sizeof(colors) / sizeof(colors[0]);
    int16_t max_colors = 256;
    if (fc.is_themed) {
        if (fc.x.theme_index < 0 || fc.x.theme_index >= max_colors) {
            fc.x.theme_index = deflt;
        }
    }
    return fc;
}

static bool poll_event(SDL_Event* data) {
    ZoneScoped;
    return SDL_PollEvent(data);
}

struct Scroll_State {
    int scrolling;            // flag (scrolling or not)
    int sensitivity = 40;     // how fast we want to scroll
    double y = 0;             // current scrolling amount (on Y-Axis)
    double acceleration;      // scrolling speed
    double friction = 0.001;  // how fast we decelerate
    double prev_pos;          // previous event's position
};

static float get_dpi_scale(SDL_Window* window) {
    int display = SDL_GetWindowDisplayIndex(window);
    if (display == -1)
        display = 0;

    const float dpi_default = 96.0f;
    float dpi = 0;
    if (SDL_GetDisplayDPI(display, &dpi, NULL, NULL) != 0)
        return 1.0f;  // failure so assume no scaling
    return dpi / dpi_default;
}

static uint16_t convert_key_modifiers(int mod) {
    uint16_t modifiers = 0;
    if (mod & KMOD_CTRL) {
        modifiers |= CONTROL;
    }
    if (mod & KMOD_ALT) {
        modifiers |= ALT;
    }
    if (mod & KMOD_SHIFT) {
        modifiers |= SHIFT;
    }
    if (mod & KMOD_GUI) {
        modifiers |= GUI;
    }
    return modifiers;
}

static void process_event(Server* server,
                          Client* client,
                          SDL_Event event,
                          Scroll_State* scroll,
                          SDL_Window* window,
                          float* dpi_scale,
                          int* mod_state,
                          int character_width,
                          int character_height,
                          bool* force_redraw,
                          bool* minimized,
                          bool* disable_key_presses,
                          bool* previous_alt_u,
                          uint32_t* disable_key_presses_until) {
    ZoneScoped;

    bool is_alt_u = false;

    // Handle modifier changes before the timeout check.
    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
#define MOD_CASE(name)             \
    case SDLK_##name:              \
        *mod_state |= KMOD_##name; \
        return

            // Held modifier keys.
            MOD_CASE(LCTRL);
            MOD_CASE(RCTRL);
            MOD_CASE(LSHIFT);
            MOD_CASE(RSHIFT);
            MOD_CASE(LALT);
            MOD_CASE(RALT);
            MOD_CASE(LGUI);
            MOD_CASE(RGUI);
            MOD_CASE(MODE);

#undef MOD_CASE

            // Toggle modifiers keys.
        case SDLK_NUMLOCKCLEAR:
            *mod_state ^= KMOD_NUM;
            return;
        case SDLK_CAPSLOCK:
            *mod_state ^= KMOD_CAPS;
            return;
        }
    } else if (event.type == SDL_KEYUP) {
        switch (event.key.keysym.sym) {
#define MOD_CASE(name)              \
    case SDLK_##name:               \
        *mod_state &= ~KMOD_##name; \
        return

            // Held modifier keys.
            MOD_CASE(LCTRL);
            MOD_CASE(RCTRL);
            MOD_CASE(LSHIFT);
            MOD_CASE(RSHIFT);
            MOD_CASE(LALT);
            MOD_CASE(RALT);
            MOD_CASE(LGUI);
            MOD_CASE(RGUI);
            MOD_CASE(MODE);

#undef MOD_CASE
        }
    }

    // See comment on disable_key_presses's declaration.
    if (*disable_key_presses && event.type != SDL_QUIT) {
        if (event.text.timestamp > *disable_key_presses_until) {
            *disable_key_presses = false;
        } else {
            return;
        }
    }

    // Handle the event.
    switch (event.type) {
    case SDL_QUIT:
        client->queue_quit = true;
        break;

    case SDL_TEXTINPUT:
        if (!(*mod_state & ~(KMOD_SHIFT | KMOD_CAPS | KMOD_NUM | KMOD_GUI))) {
#ifdef __APPLE__
            // On mac, A-u will try to push an umlaut on the next character.  We don't like
            // this behavior because 1.  we don't have utf8 support, and 2.  we use A-u often.
            if (*previous_alt_u) {
                // Character after A-u doesn't work with an umlaut so
                // the mac will emit an umlaut (U+00A8) by itself.  Ignore it.
                if (event.text.text[0] == -62 && event.text.text[1] == -88)
                    break;

                // Try to remove the umlaut.
                if (event.text.text[0] == -61) {
                    switch (event.text.text[1]) {
                    case -124:
                        // A with umlaut (U+00C4)
                        event.text.text[0] = 'A';
                        event.text.text[1] = '\0';
                        break;
                    case -117:
                        // E with umlaut (U+00CB)
                        event.text.text[0] = 'E';
                        event.text.text[1] = '\0';
                        break;
                    case -113:
                        // I with umlaut (U+00CF)
                        event.text.text[0] = 'I';
                        event.text.text[1] = '\0';
                        break;
                    case -106:
                        // O with umlaut (U+00D6)
                        event.text.text[0] = 'O';
                        event.text.text[1] = '\0';
                        break;
                    case -100:
                        // U with umlaut (U+00DC)
                        event.text.text[0] = 'U';
                        event.text.text[1] = '\0';
                        break;
                    case -92:
                        // a with umlaut (U+00E4)
                        event.text.text[0] = 'a';
                        event.text.text[1] = '\0';
                        break;
                    case -85:
                        // e with umlaut (U+00EB)
                        event.text.text[0] = 'e';
                        event.text.text[1] = '\0';
                        break;
                    case -81:
                        // i with umlaut (U+00EF)
                        event.text.text[0] = 'i';
                        event.text.text[1] = '\0';
                        break;
                    case -74:
                        // o with umlaut (U+00F6)
                        event.text.text[0] = 'o';
                        event.text.text[1] = '\0';
                        break;
                    case -68:
                        // u with umlaut (U+00FC)
                        event.text.text[0] = 'u';
                        event.text.text[1] = '\0';
                        break;
                    case -65:
                        // y with umlaut (U+00FF)
                        event.text.text[0] = 'y';
                        event.text.text[1] = '\0';
                        break;
                    }
                } else if (event.text.text[0] == -59) {
                    if (event.text.text[1] == -72) {
                        // Y with umlaut (U+0178)
                        event.text.text[0] = 'Y';
                        event.text.text[1] = '\0';
                    }
                }
            }
#endif

            for (char* p = event.text.text; *p; ++p) {
                Key key = {};
                key.code = *p;
                server->receive(client, key);
            }
        }
        break;

    case SDL_TEXTEDITING:
        if (event.edit.length == 0) {
            is_alt_u = *previous_alt_u;
            break;
        }

        printf("TODO: handle SDL_TEXTEDITING\n");
        printf("Edit: text: %s, start: %" PRIi32 ", length: %" PRIi32 "\n", event.edit.text,
               event.edit.start, event.edit.length);

        break;

    case SDL_WINDOWEVENT: {
        switch (event.window.event) {
        case SDL_WINDOWEVENT_MOVED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_EXPOSED: {
            float new_dpi_scale = get_dpi_scale(window);
            bool dpi_changed = (*dpi_scale + 0.01f < new_dpi_scale ||  //
                                *dpi_scale - 0.01f > new_dpi_scale);
            if (dpi_changed) {
                int w, h;
                SDL_GetWindowSize(window, &w, &h);
                w = (int)(w * (new_dpi_scale / *dpi_scale));
                h = (int)(h * (new_dpi_scale / *dpi_scale));
                SDL_SetWindowSize(window, w, h);
                *dpi_scale = new_dpi_scale;
            }
            *force_redraw = true;
        } break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            // See comment on disable_key_presses's declaration.
            *disable_key_presses = true;
            *disable_key_presses_until = event.window.timestamp + 100;
            break;
        case SDL_WINDOWEVENT_MINIMIZED:
            *minimized = true;
            break;
        }
        break;
    }

    case SDL_DROPFILE: {
        size_t len = strlen(event.drop.file);
        cz::path::convert_to_forward_slashes(event.drop.file, len);
        open_file(&server->editor, client, {event.drop.file, len});
        SDL_free(event.drop.file);
        break;
    }

    case SDL_MOUSEWHEEL: {
        Window_Unified* window = client->selected_normal_window;
        CZ_DEFER(client->selected_normal_window = window);
        if (client->mouse.window && client->mouse.window->tag == Window::UNIFIED) {
            client->selected_normal_window = (Window_Unified*)client->mouse.window;
        }

        bool mini = client->_select_mini_buffer;
        CZ_DEFER(client->_select_mini_buffer = mini);
        client->_select_mini_buffer = false;

        Key key = {};
        key.modifiers = convert_key_modifiers(*mod_state);
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            event.wheel.y *= -1;
            event.wheel.x *= -1;
        }

// On linux the horizontal scroll is flipped for some reason.
#ifndef _WIN32
        event.wheel.x *= -1;
#endif
#ifdef __APPLE__
        event.wheel.y *= -1;
#endif

        for (int y = 0; y < event.wheel.y; ++y) {
            key.code = Key_Code::SCROLL_UP;
            server->receive(client, key);
        }
        for (int y = 0; y > event.wheel.y; --y) {
            key.code = Key_Code::SCROLL_DOWN;
            server->receive(client, key);
        }
        for (int x = 0; x < event.wheel.x; ++x) {
            key.code = Key_Code::SCROLL_RIGHT;
            server->receive(client, key);
        }
        for (int x = 0; x > event.wheel.x; --x) {
            key.code = Key_Code::SCROLL_LEFT;
            server->receive(client, key);
        }
        break;
    }

    case SDL_MULTIGESTURE: {
        if (event.mgesture.numFingers == 2) {
            if (scroll->scrolling == 0) {
                scroll->y = 0;
            } else {
                double dy = event.mgesture.y - scroll->prev_pos;
                scroll->acceleration = dy * 40;
            }

            scroll->prev_pos = event.mgesture.y;
            scroll->scrolling = 1;
        }
        break;
    }

    case SDL_FINGERDOWN: {
        scroll->scrolling = 0;
        break;
    }

    case SDL_MOUSEMOTION: {
        client->mouse.has_client_position = true;
        client->mouse.client_row = event.motion.y / character_height;
        client->mouse.client_column = event.motion.x / character_width;
        recalculate_mouse(server->editor.theme, client);
        break;
    }

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        Key key = {};
        key.modifiers = convert_key_modifiers(*mod_state);

        if (event.button.button == SDL_BUTTON_LEFT) {
            key.code = Key_Code::MOUSE1;
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            key.code = Key_Code::MOUSE2;
        } else if (event.button.button == SDL_BUTTON_MIDDLE) {
            key.code = Key_Code::MOUSE3;
        } else if (event.button.button == SDL_BUTTON_X1) {
            key.code = Key_Code::MOUSE4;
        } else if (event.button.button == SDL_BUTTON_X2) {
            key.code = Key_Code::MOUSE5;
        } else {
            break;
        }

        if (event.button.state == SDL_PRESSED) {
            server->receive(client, key);
        } else {
            server->release(client, key);
        }
        break;
    }

    case SDL_KEYDOWN: {
        Key key = {};
        switch (event.key.keysym.sym) {
#define KEY_CASE(name, value) \
    case SDLK_##name:         \
        key.code = value;     \
        break

            KEY_CASE(SPACE, ' ');
            KEY_CASE(BACKSPACE, Key_Code::BACKSPACE);
            KEY_CASE(INSERT, Key_Code::INSERT);
            KEY_CASE(DELETE, Key_Code::DELETE_);
            KEY_CASE(HOME, Key_Code::HOME);
            KEY_CASE(END, Key_Code::END);
            KEY_CASE(PAGEUP, Key_Code::PAGE_UP);
            KEY_CASE(PAGEDOWN, Key_Code::PAGE_DOWN);
            KEY_CASE(ESCAPE, Key_Code::ESCAPE);
            KEY_CASE(RETURN, '\n');
            KEY_CASE(TAB, '\t');
            KEY_CASE(UP, Key_Code::UP);
            KEY_CASE(DOWN, Key_Code::DOWN);
            KEY_CASE(LEFT, Key_Code::LEFT);
            KEY_CASE(RIGHT, Key_Code::RIGHT);
            KEY_CASE(F1, Key_Code::F1);
            KEY_CASE(F2, Key_Code::F2);
            KEY_CASE(F3, Key_Code::F3);
            KEY_CASE(F4, Key_Code::F4);
            KEY_CASE(F5, Key_Code::F5);
            KEY_CASE(F6, Key_Code::F6);
            KEY_CASE(F7, Key_Code::F7);
            KEY_CASE(F8, Key_Code::F8);
            KEY_CASE(F9, Key_Code::F9);
            KEY_CASE(F10, Key_Code::F10);
            KEY_CASE(F11, Key_Code::F11);
            KEY_CASE(F12, Key_Code::F12);
            KEY_CASE(MENU, Key_Code::MENU);
            KEY_CASE(SCROLLLOCK, Key_Code::SCROLL_LOCK);

            // Keypad keys
            KEY_CASE(KP_DIVIDE, '/');
            KEY_CASE(KP_MULTIPLY, '*');
            KEY_CASE(KP_MINUS, '-');
            KEY_CASE(KP_PLUS, '+');
            KEY_CASE(KP_ENTER, '\n');
            KEY_CASE(KP_1, '1');
            KEY_CASE(KP_2, '2');
            KEY_CASE(KP_3, '3');
            KEY_CASE(KP_4, '4');
            KEY_CASE(KP_5, '5');
            KEY_CASE(KP_6, '6');
            KEY_CASE(KP_7, '7');
            KEY_CASE(KP_8, '8');
            KEY_CASE(KP_9, '9');
            KEY_CASE(KP_0, '0');
            KEY_CASE(KP_PERIOD, '.');

#undef KEY_CASE

        default: {
            cz::Str key_name = SDL_GetKeyName(event.key.keysym.sym);
            if (key_name.len == 1) {
                key.code = key_name[0];
            } else {
                fprintf(stderr, "Unhandled key '%s'\n", key_name.buffer);
                return;
            }
        } break;
        }

        // Transform capslock to holding shift.
        if (event.key.keysym.mod & KMOD_CAPS) {
            event.key.keysym.mod &= ~KMOD_CAPS;
            event.key.keysym.mod |= KMOD_SHIFT;
        }
        // Ignore numlock.
        event.key.keysym.mod &= ~KMOD_NUM;

        if ((event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) == 0 &&
            (key.code <= UCHAR_MAX && cz::is_print((unsigned char)key.code))) {
            // Ignore key presses
            return;
        }

        if (event.key.keysym.mod & KMOD_GUI) {
            key.modifiers |= GUI;
        }
        if (event.key.keysym.mod & KMOD_CTRL) {
            key.modifiers |= CONTROL;
        }
        if (event.key.keysym.mod & KMOD_ALT) {
            key.modifiers |= ALT;
        }
        if (event.key.keysym.mod & KMOD_SHIFT) {
            switch (key.code) {
#define UPPER(l, u)   \
    case l:           \
        key.code = u; \
        break

                UPPER('\'', '"');
                UPPER(',', '<');
                UPPER('.', '>');
                UPPER('1', '!');
                UPPER('2', '@');
                UPPER('3', '#');
                UPPER('4', '$');
                UPPER('5', '%');
                UPPER('6', '^');
                UPPER('7', '&');
                UPPER('8', '*');
                UPPER('9', '(');
                UPPER('0', ')');
                UPPER('[', '{');
                UPPER(']', '}');
                UPPER(';', ':');
                UPPER('/', '?');
                UPPER('\\', '|');
                UPPER('=', '+');
                UPPER('-', '_');
                UPPER('`', '~');

#undef UPPER

            default:
                if (key.code <= UCHAR_MAX && cz::is_alpha((unsigned char)key.code)) {
                    // It's already upper case
                } else {
                    key.modifiers |= SHIFT;
                }
                break;
            }
        } else {
            if (key.code <= UCHAR_MAX) {
                key.code = cz::to_lower((unsigned char)key.code);
            }
        }

        is_alt_u = (key.modifiers == ALT && key.code == 'u');

        server->receive(client, key);
    } break;

    case SDL_KEYUP:
        is_alt_u = *previous_alt_u;
        break;
    }

    *previous_alt_u = is_alt_u;
}

static void process_events(Server* server,
                           Client* client,
                           Scroll_State* scroll,
                           SDL_Window* window,
                           float* dpi_scale,
                           int* mod_state,
                           int character_width,
                           int character_height,
                           bool* force_redraw,
                           bool* minimized,
                           bool* previous_alt_u,
                           bool* disable_key_presses,
                           uint32_t* disable_key_presses_until) {
    ZoneScoped;

    SDL_Event event;
    while (poll_event(&event)) {
        process_event(server, client, event, scroll, window, dpi_scale, mod_state, character_width,
                      character_height, force_redraw, minimized, previous_alt_u,
                      disable_key_presses, disable_key_presses_until);
        if (client->queue_quit) {
            return;
        }
    }
}

static void process_scroll(Server* server, Client* client, Scroll_State* scroll, int mod_state) {
    if (scroll->scrolling) {
        if (scroll->acceleration > 0) {
            scroll->acceleration -= scroll->friction;
        }
        if (scroll->acceleration < 0) {
            scroll->acceleration += scroll->friction;
        }
        if (abs(scroll->acceleration) < 0.0005) {
            scroll->acceleration = 0;
        }

        scroll->y += scroll->sensitivity * scroll->acceleration;

        Key key = {};
        key.modifiers = convert_key_modifiers(mod_state);
        while (scroll->y <= -1) {
            scroll->y += 1;
            key.code = Key_Code::SCROLL_DOWN_ONE;
            server->receive(client, key);
        }
        while (scroll->y >= 1) {
            scroll->y -= 1;
            key.code = Key_Code::SCROLL_UP_ONE;
            server->receive(client, key);
        }
    }
}

static void blit_surface(SDL_Surface* surface, SDL_Surface* rendered_char, SDL_Rect* rect) {
    ZoneScopedN("SDL_BlitSurface");
    SDL_BlitSurface(rendered_char, nullptr, surface, rect);
}

static void draw_corner(SDL_Surface* bg,
                        SDL_Color color,
                        int x,
                        int y,
                        int radius,
                        int radiusp12,
                        int radius2) {
    //    rx ->
    // ry     |
    // |    .^
    // v  -^
    for (int ry = 0; ry < radius; ++ry) {
        for (int rx = 0; rx < radius; ++rx) {
            int rad = rx * rx + ry * ry;
            if (rad < radius2) {
                // Totally Inside
                continue;
            }

            char* iter = (char*)bg->pixels;
            if (y)
                iter += (y + ry) * bg->pitch;
            else
                iter += (radius - ry - 1) * bg->pitch;
            if (x)
                iter += (x + rx) * bg->format->BytesPerPixel;
            else
                iter += (radius - rx - 1) * bg->format->BytesPerPixel;

            // Totally Outside
            if (rad >= radiusp12) {
                *(uint32_t*)iter = 0;
                continue;
            }

            uint8_t a = (uint8_t)((color.a * (radiusp12 - rad)) / (radiusp12 - radius2));
            uint32_t mp = SDL_MapRGBA(bg->format, color.r, color.g, color.b, a);
            *(uint32_t*)iter = mp;
        }
    }
}

static void render_background(SDL_Surface* surface,
                              SDL_Color color,
                              SDL_Rect rect,
                              SDL_Color default_background,
                              bool round_top_left,
                              bool round_top_right,
                              bool round_bottom_left,
                              bool round_bottom_right) {
    ZoneScoped;

    uint32_t color32 = SDL_MapRGBA(surface->format, color.r, color.g, color.b, color.a);
    uint32_t default_background32 =
        SDL_MapRGBA(surface->format, default_background.r, default_background.g,
                    default_background.b, default_background.a);

    if (color32 == default_background32 ||
        (!round_top_left && !round_top_right && !round_bottom_left && !round_bottom_right)) {
        SDL_FillRect(surface, &rect, color32);
        return;
    }

    SDL_FillRect(surface, &rect, default_background32);

    // TODO cache the surfaces
    SDL_Surface* bg = SDL_CreateRGBSurfaceWithFormat(0, rect.w, rect.h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_SetSurfaceBlendMode(bg, SDL_BLENDMODE_BLEND);

    SDL_FillRect(bg, nullptr, SDL_MapRGBA(bg->format, color.r, color.g, color.b, color.a));

    int radius = (int)((float)rect.w / 2.0f);
    int radius2 = (radius - 1) * (radius - 1);
    int radiusp12 = radius * radius;
    if (round_top_left) {
        draw_corner(bg, color, 0, 0, radius, radiusp12, radius2);
    }
    if (round_top_right) {
        draw_corner(bg, color, rect.w - radius, 0, radius, radiusp12, radius2);
    }
    if (round_bottom_left) {
        draw_corner(bg, color, 0, rect.h - radius, radius, radiusp12, radius2);
    }
    if (round_bottom_right) {
        draw_corner(bg, color, rect.w - radius, rect.h - radius, radius, radiusp12, radius2);
    }

    blit_surface(surface, bg, &rect);

    SDL_FreeSurface(bg);
}

static Face_Color bg_color_of(const Cell& new_cell) {
    if (new_cell.face.flags & Face::REVERSE) {
        Face_Color bg = new_cell.face.foreground;
        return get_face_color_or(bg, 7);
    } else {
        Face_Color bg = new_cell.face.background;
        return get_face_color_or(bg, 0);
    }
}

static void render(SDL_Window* window,
                   TTF_Font* font,
                   SDL_Surface**** surface_cache,
                   int* old_width,
                   int* old_height,
                   int* total_rows,
                   int* total_cols,
                   int character_width,
                   int character_height,
                   Cell** cellss,
                   Window_Cache** window_cache,
                   Window_Cache** mini_buffer_window_cache,
                   Editor* editor,
                   Client* client,
                   bool force_redraw,
                   bool* redrew) {
    ZoneScoped;

    SDL_Surface* surface;
    {
        ZoneScopedN("SDL_GetWindowSurface");
        surface = SDL_GetWindowSurface(window);
    }

    int rows, cols;
    {
        ZoneScopedN("detect resize");
        int width, height;
        SDL_GetWindowSize(window, &width, &height);
        cols = width / character_width;
        rows = height / character_height;

        if (force_redraw || width != *old_width || height != *old_height) {
            force_redraw = true;
            *old_width = width;
            *old_height = height;

            SDL_Color bgc = make_color(editor->theme.colors, 0);
            SDL_FillRect(surface, nullptr,
                         SDL_MapRGBA(surface->format, bgc.r, bgc.g, bgc.b, bgc.a));

            destroy_window_cache(*window_cache);
            *window_cache = nullptr;
            destroy_window_cache(*mini_buffer_window_cache);
            *mini_buffer_window_cache = nullptr;

            cz::heap_allocator().dealloc(cellss[0], *total_rows * *total_cols);
            cz::heap_allocator().dealloc(cellss[1], *total_rows * *total_cols);

            *total_rows = rows;
            *total_cols = cols;

            size_t grid_size = (size_t)rows * cols;
            cellss[0] = cz::heap_allocator().alloc<Cell>(grid_size);
            cellss[1] = cz::heap_allocator().alloc<Cell>(grid_size);

            for (size_t i = 0; i < grid_size; ++i) {
                cellss[0][i].face = {};
                cellss[0][i].code = ' ';
                cellss[1][i].face = {};
                cellss[1][i].code = ' ';
            }
        }
    }

    bool any_animated_scrolling = false;
    render_to_cells(cellss[1], window_cache, mini_buffer_window_cache, rows, cols, editor, client,
                    &any_animated_scrolling);

    bool any_changes = any_animated_scrolling;
    const SDL_Color default_background = make_color(editor->theme.colors, {0});

    {
        ZoneScopedN("draw cells");

        for (int y = 0, index = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x, ++index) {
                Cell* new_cell = &cellss[1][index];
                if (!force_redraw && cellss[0][index] == *new_cell &&
                    (x == 0 || cellss[0][index - 1] == cellss[1][index - 1]) &&
                    (x + 1 == cols || cellss[0][index + 1] == cellss[1][index + 1]) &&
                    (y == 0 || cellss[0][index - cols] == cellss[1][index - cols]) &&
                    (y + 1 == rows || cellss[0][index + cols] == cellss[1][index + cols])) {
                    continue;
                }

                any_changes = true;

                ZoneScopedN("draw cell background");

                Face_Color bg = bg_color_of(*new_cell);

                // clang-format off
                bool match_left   = (x     != 0    && bg_color_of(cellss[1][index - 1])    == bg);
                bool match_right  = (x + 1 != cols && bg_color_of(cellss[1][index + 1])    == bg);
                bool match_top    = (y     != 0    && bg_color_of(cellss[1][index - cols]) == bg);
                bool match_bottom = (y + 1 != rows && bg_color_of(cellss[1][index + cols]) == bg);

                bool round_top_left     = !match_top    && !match_left;
                bool round_top_right    = !match_top    && !match_right;
                bool round_bottom_left  = !match_bottom && !match_left;
                bool round_bottom_right = !match_bottom && !match_right;
                // clang-format on

                SDL_Rect rect;
                rect.x = x * character_width;
                rect.y = y * character_height;
                rect.w = character_width;
                rect.h = character_height;

                SDL_Color bgc = make_color(editor->theme.colors, bg);

                render_background(surface, bgc, rect, default_background, round_top_left,
                                  round_top_right, round_bottom_left, round_bottom_right);
                bgc = {};
            }
        }

        for (int y = 0, index = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x, ++index) {
                Cell* new_cell = &cellss[1][index];
                if (!force_redraw && cellss[0][index] == *new_cell &&
                    (x == 0 || cellss[0][index - 1] == cellss[1][index - 1]) &&
                    (x + 1 == cols || cellss[0][index + 1] == cellss[1][index + 1]) &&
                    (y == 0 || cellss[0][index - cols] == cellss[1][index - cols]) &&
                    (y + 1 == rows || cellss[0][index + cols] == cellss[1][index + cols])) {
                    continue;
                }

                ZoneScopedN("draw cell foreground");

                Face_Color fg = (new_cell->face.flags & Face::REVERSE) ? new_cell->face.background
                                                                       : new_cell->face.foreground;
                fg = get_face_color_or(fg, (new_cell->face.flags & Face::REVERSE) ? 0 : 7);

                SDL_Rect rect;
                rect.x = x * character_width;
                rect.y = y * character_height;
                rect.w = character_width;
                rect.h = character_height;

                SDL_Color fgc = make_color(editor->theme.colors, fg);

                // We only cache themed colors.
                SDL_Surface* rendered_char;
                if (fg.is_themed) {
                    rendered_char = lookup_surface_cache(
                        surface_cache, font, !!(new_cell->face.flags & Face::UNDERSCORE),
                        !!(new_cell->face.flags & Face::BOLD),
                        !!(new_cell->face.flags & Face::ITALICS), fg.x.theme_index, fgc,
                        new_cell->code);
                } else {
                    rendered_char = render_character(font, new_cell->code,
                                                     !!(new_cell->face.flags & Face::UNDERSCORE),
                                                     !!(new_cell->face.flags & Face::BOLD),
                                                     !!(new_cell->face.flags & Face::ITALICS), fgc);
                }

                if (!rendered_char) {
                    fprintf(stderr, "Failed to render text '%c': %s\n", new_cell->code,
                            TTF_GetError());
                    continue;
                }

                blit_surface(surface, rendered_char, &rect);
            }
        }
    }

    if (any_changes) {
        cz::swap(cellss[0], cellss[1]);

        ZoneScopedN("SDL_UpdateWindowSurface");
        SDL_UpdateWindowSurface(window);

        *redrew = true;
    }
}

static bool get_character_dims(TTF_Font* font, int* character_width, int* character_height) {
    SDL_Surface* surface = TTF_RenderText_Blended(font, "c", {0x00, 0x00, 0x00, 0xff});
    if (!surface) {
        fprintf(stderr, "Failed to render any text: %s\n", SDL_GetError());
        return false;
    }
    CZ_DEFER(SDL_FreeSurface(surface));

    *character_width = surface->w;
    *character_height = surface->h;

    return true;
}

static bool get_clipboard(void*, cz::Allocator allocator, cz::String* text) {
    ZoneScoped;

    char* ptr = SDL_GetClipboardText();
    if (!ptr) {
        return false;
    }
    CZ_DEFER(SDL_free(ptr));
    if (!ptr[0]) {
        return false;
    }

    cz::Str str = ptr;
#ifdef _WIN32
    cz::strip_carriage_returns(ptr, &str.len);
#endif

    text->reserve(allocator, str.len);
    text->append(str);
    return true;
}

static bool set_clipboard(void*, cz::Str text) {
    cz::String copy = text.clone_null_terminate(cz::heap_allocator());
    CZ_DEFER(copy.drop(cz::heap_allocator()));
    return SDL_SetClipboardText(copy.buffer) >= 0;
}

void set_icon(SDL_Window* sdl_window) {
    ZoneScoped;

#ifdef _WIN32
    SDL_SysWMinfo wminfo;
    SDL_VERSION(&wminfo.version);
    if (SDL_GetWindowWMInfo(sdl_window, &wminfo) == 1) {
        HWND hwnd = wminfo.info.win.window;

        HINSTANCE handle = ::GetModuleHandle(nullptr);
        HICON icon = ::LoadIcon(handle, "IDI_MAIN_ICON");
        if (icon != nullptr) {
            ::SetClassLongPtr(hwnd, GCLP_HICON, (LONG_PTR)icon);
        }
    }
#else
    cz::String logo = {};
    logo.reserve(cz::heap_allocator(), program_dir.len + 8 + 1);
    CZ_DEFER(logo.drop(cz::heap_allocator()));
    logo.append(program_dir);
    logo.append("logo.png");
    logo.null_terminate();

    SDL_Surface* icon = IMG_Load(logo.buffer);
    if (icon) {
        SDL_SetWindowIcon(sdl_window, icon);
        SDL_FreeSurface(icon);
    }
#endif
}

static bool load_font(cz::String* font_file,
                      uint32_t* font_size,
                      TTF_Font** font,
                      int* character_width,
                      int* character_height,
                      SDL_Surface**** surface_cache,
                      cz::Str new_font_file,
                      uint32_t new_font_size,
                      float dpi_scale) {
    ZoneScoped;

    // Set font config variables even if we fail to load the font
    // so we don't keep looping over and over trying to load it.
    font_file->reserve(cz::heap_allocator(), new_font_file.len + 1);
    font_file->append(new_font_file);
    font_file->null_terminate();
    *font_size = new_font_size;

    // Load the font.
    TTF_Font* new_font = TTF_OpenFont(font_file->buffer, (int)(*font_size * dpi_scale));
    if (!new_font) {
        fprintf(stderr, "Failed to open the font file '%s': %s\n", font_file->buffer,
                SDL_GetError());
        return false;
    }

    if (!get_character_dims(new_font, character_width, character_height)) {
        TTF_CloseFont(new_font);
        return false;
    }

    if (*font) {
        TTF_CloseFont(*font);
    }
    *font = new_font;

    drop_surface_cache(surface_cache);
    memset(surface_cache, 0, 256 * sizeof(*surface_cache));
    return true;
}

void run(Server* server, Client* client) {
    server->set_async_locked(false);
    // @Unlocked No usage of server or client can be made in this region!!!

#ifdef _WIN32
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
#endif

    int result;

    {
        ZoneScopedN("SDL_Init");
        result = SDL_Init(SDL_INIT_VIDEO);
    }
    if (result != 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return;
    }
    CZ_DEFER(SDL_Quit());

    {
        ZoneScopedN("TTF_Init");
        result = TTF_Init();
    }
    if (result != 0) {
        fprintf(stderr, "Failed to initialize TTF: %s\n", TTF_GetError());
        return;
    }
    CZ_DEFER(TTF_Quit());

    int img_init_flags = IMG_INIT_PNG;
    {
        ZoneScopedN("IMG_Init");
        result = IMG_Init(img_init_flags);
    }
    if (result != img_init_flags) {
        fprintf(stderr, "Failed to initialize IMG: %s\n", IMG_GetError());
        return;
    }
    CZ_DEFER(IMG_Quit());

    float dpi_scale = get_dpi_scale(NULL);
    float old_dpi_scale = dpi_scale;

    const char* window_name = "Mag";
#ifndef NDEBUG
    window_name = "Mag [DEBUG]";
#endif

    SDL_Window* window;
    {
        ZoneScopedN("SDL_CreateWindow");
        window =
            SDL_CreateWindow(window_name, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             (int)(800 * dpi_scale), (int)(800 * dpi_scale),
                             SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    }
    if (!window) {
        fprintf(stderr, "Failed to create a window: %s\n", SDL_GetError());
        return;
    }
    CZ_DEFER(SDL_DestroyWindow(window));

    set_icon(window);

    // @Unlocked End of region
    server->set_async_locked(true);

    // All the font variables.
    cz::String font_file = {};
    CZ_DEFER(font_file.drop(cz::heap_allocator()));
    uint32_t font_size = 0;
    TTF_Font* font = nullptr;
    CZ_DEFER(if (font) TTF_CloseFont(font));

    int character_width;
    int character_height;

    // SDL_Surface* surface_cache[fg.theme_index(256)][Face::flags(8)][char(UCHAR_MAX + 1)]
    SDL_Surface*** surface_cache[256] = {};
    CZ_DEFER(drop_surface_cache(surface_cache));

    // Load the font.
    if (!load_font(&font_file, &font_size, &font, &character_width, &character_height,
                   surface_cache, server->editor.theme.font_file, server->editor.theme.font_size,
                   dpi_scale)) {
        return;
    }

    int old_width = 0;
    int old_height = 0;
    int total_rows = 0;
    int total_cols = 0;

    Cell* cellss[2] = {nullptr, nullptr};
    CZ_DEFER({
        cz::heap_allocator().dealloc(cellss[0], total_rows * total_cols);
        cz::heap_allocator().dealloc(cellss[1], total_rows * total_cols);
    });

    Window_Cache* window_cache = nullptr;
    CZ_DEFER(destroy_window_cache(window_cache));
    Window_Cache* mini_buffer_window_cache = nullptr;
    CZ_DEFER(destroy_window_cache(mini_buffer_window_cache));

    SDL_StartTextInput();
    CZ_DEFER(SDL_StopTextInput());

    // If no redraws have happened in a long time then increase the frame length.
    uint32_t redrew_last = 0;

    client->set_system_clipboard_func = set_clipboard;
    client->set_system_clipboard_data = nullptr;
    client->get_system_clipboard_func = get_clipboard;
    client->get_system_clipboard_data = nullptr;

    Scroll_State scroll = {};

    int mod_state = 0;

    bool minimized = false;
    bool force_redraw = false;
    bool previous_alt_u = false;

    // There is a super freaking annoying bug that we will gain focus and get
    // a key press that the user was pressing on another window.  So just
    // disable key presses for a split second when we gain focus.
    bool disable_key_presses = false;
    uint32_t disable_key_presses_until;

    custom::client_created_callback(&server->editor, client);

    while (1) {
        ZoneScopedN("sdl main loop");

        uint32_t frame_start_ticks = SDL_GetTicks();

        bool any_asynchronous_jobs = server->slurp_jobs();
        bool any_synchronous_jobs = server->run_synchronous_jobs(client);

        process_events(server, client, &scroll, window, &dpi_scale, &mod_state, character_width,
                       character_height, &force_redraw, &minimized, &previous_alt_u,
                       &disable_key_presses, &disable_key_presses_until);

        any_asynchronous_jobs |= server->send_pending_asynchronous_jobs();

        process_scroll(server, client, &scroll, mod_state);

        process_buffer_external_updates(&server->editor, client, client->window);

        server->process_key_chain(client);

        if (client->queue_quit) {
            break;
        }

        // If the font info was updated then reload the font.
        cz::Str new_font_file = server->editor.theme.font_file;
        if (font_file != new_font_file || font_size != server->editor.theme.font_size ||
            dpi_scale != old_dpi_scale) {
            font_file.len = 0;

            // If loading the font fails then we print a message inside `load_font()` and continue.
            if (load_font(&font_file, &font_size, &font, &character_width, &character_height,
                          surface_cache, server->editor.theme.font_file,
                          server->editor.theme.font_size, dpi_scale)) {
                // Redraw the screen with the new font info.
                force_redraw = true;
            }
        }

        client->update_mini_buffer_completion_cache();
        load_mini_buffer_completion_cache(server, client);

        if (client->_pending_raise) {
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
                SDL_RestoreWindow(window);
            SDL_RaiseWindow(window);
            client->_pending_raise = false;
        }

        bool redrew_this_time = false;
        render(window, font, surface_cache, &old_width, &old_height, &total_rows, &total_cols,
               character_width, character_height, cellss, &window_cache, &mini_buffer_window_cache,
               &server->editor, client, force_redraw, &redrew_this_time);

        force_redraw = false;

        any_asynchronous_jobs |= server->send_pending_asynchronous_jobs();

        uint32_t frame_end_ticks = SDL_GetTicks();

        // 60 fps is default.
        uint32_t frame_length = (uint32_t)(1000.0f / 60.0f);
        bool no_jobs = !(any_asynchronous_jobs || any_synchronous_jobs);
        if (redrew_this_time) {
            // Record that we redrew.
            redrew_last = frame_end_ticks;
        } else if (minimized || (no_jobs && redrew_last + 600000 < frame_end_ticks)) {
            // If we are minimized or if 10 minutes have elapsed with no
            // jobs still running then lower the frame rate to 1 fps.
            frame_length = (uint32_t)(1000.0f / 1.0f);
        } else if (redrew_last + 1000 < frame_end_ticks) {
            // If one second has elapsed then lower the frame rate to 8 fps.
            frame_length = (uint32_t)(1000.0f / 8.0f);
        }

        uint32_t elapsed_ticks = frame_end_ticks - frame_start_ticks;
        if (elapsed_ticks < frame_length) {
            SDL_Event event;
            int result;

            server->set_async_locked(false);
            {
                uint32_t sleep_time = frame_length - elapsed_ticks;
                ZoneValue(sleep_time);

                result = SDL_WaitEventTimeout(&event, sleep_time);
            }
            server->set_async_locked(true);

            if (result) {
                process_event(server, client, event, &scroll, window, &dpi_scale, &mod_state,
                              character_width, character_height, &force_redraw, &minimized,
                              &previous_alt_u, &disable_key_presses, &disable_key_presses_until);
            }
        }

        FrameMark;
    }
}

}
}
}
