#include "sdl.hpp"

#include <SDL.h>
#include <SDL_ttf.h>
#include <ctype.h>
#include <Tracy.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/str.hpp>
#include "cell.hpp"
#include "client.hpp"
#include "render.hpp"
#include "server.hpp"
#include "window_cache.hpp"

namespace mag {
namespace client {
namespace sdl {

static uint32_t colors[] = {
    0x073642,
    0xDC322F,
    0x859900,
    0xB58900,
    0x268BD2,
    0xD33682,
    0x2AA198,
    0xEEE8D5,
    0x657B83,
    0xCB4B16,
    0x586E75,
    0x657B83,
    0x839496,
    0x6C71C4,
    0x93A1A1,
    0xFDF6E3,
    //
    0x000000,
    0x00005F,
    0x000087,
    0x0000AF,
    0x0000D7,
    0x0000FF,
    //
    0x005F00,
    0x005F5F,
    0x005F87,
    0x005FAF,
    0x005FD7,
    0x005FFF,
    //
    0x008700,
    0x00875F,
    0x008787,
    0x0087AF,
    0x0087D7,
    0x0087FF,
    //
    0x00AF00,
    0x00AF5F,
    0x00AF87,
    0x00AFAF,
    0x00AFD7,
    0x00AFFF,
    //
    0x00D700,
    0x00D75F,
    0x00D787,
    0x00D7AF,
    0x00D7D7,
    0x00D7FF,
    //
    0x00FF00,
    0x00FF5F,
    0x00FF87,
    0x00FFAF,
    0x00FFD7,
    0x00FFFF,
    //
    0x5F0000,
    0x5F005F,
    0x5F0087,
    0x5F00AF,
    0x5F00D7,
    0x5F00FF,
    //
    0x5F5F00,
    0x5F5F5F,
    0x5F5F87,
    0x5F5FAF,
    0x5F5FD7,
    0x5F5FFF,
    //
    0x5F8700,
    0x5F875F,
    0x5F8787,
    0x5F87AF,
    0x5F87D7,
    0x5F87FF,
    //
    0x5FAF00,
    0x5FAF5F,
    0x5FAF87,
    0x5FAFAF,
    0x5FAFD7,
    0x5FAFFF,
    //
    0x5FD700,
    0x5FD75F,
    0x5FD787,
    0x5FD7AF,
    0x5FD7D7,
    0x5FD7FF,
    //
    0x5FFF00,
    0x5FFF5F,
    0x5FFF87,
    0x5FFFAF,
    0x5FFFD7,
    0x5FFFFF,
    //
    0x870000,
    0x87005F,
    0x870087,
    0x8700AF,
    0x8700D7,
    0x8700FF,
    //
    0x875F00,
    0x875F5F,
    0x875F87,
    0x875FAF,
    0x875FD7,
    0x875FFF,
    //
    0x878700,
    0x87875F,
    0x878787,
    0x8787AF,
    0x8787D7,
    0x8787FF,
    //
    0x87AF00,
    0x87AF5F,
    0x87AF87,
    0x87AFAF,
    0x87AFD7,
    0x87AFFF,
    //
    0x87D700,
    0x87D75F,
    0x87D787,
    0x87D7AF,
    0x87D7D7,
    0x87D7FF,
    //
    0x87FF00,
    0x87FF5F,
    0x87FF87,
    0x87FFAF,
    0x87FFD7,
    0x87FFFF,
    //
    0xAF0000,
    0xAF005F,
    0xAF0087,
    0xAF00AF,
    0xAF00D7,
    0xAF00FF,
    //
    0xAF5F00,
    0xAF5F5F,
    0xAF5F87,
    0xAF5FAF,
    0xAF5FD7,
    0xAF5FFF,
    //
    0xAF8700,
    0xAF875F,
    0xAF8787,
    0xAF87AF,
    0xAF87D7,
    0xAF87FF,
    //
    0xAFAF00,
    0xAFAF5F,
    0xAFAF87,
    0xAFAFAF,
    0xAFAFD7,
    0xAFAFFF,
    //
    0xAFD700,
    0xAFD75F,
    0xAFD787,
    0xAFD7AF,
    0xAFD7D7,
    0xAFD7FF,
    //
    0xAFFF00,
    0xAFFF5F,
    0xAFFF87,
    0xAFFFAF,
    0xAFFFD7,
    0xAFFFFF,
    //
    0xD70000,
    0xD7005F,
    0xD70087,
    0xD700AF,
    0xD700D7,
    0xD700FF,
    //
    0xD75F00,
    0xD75F5F,
    0xD75F87,
    0xD75FAF,
    0xD75FD7,
    0xD75FFF,
    //
    0xD78700,
    0xD7875F,
    0xD78787,
    0xD787AF,
    0xD787D7,
    0xD787FF,
    //
    0xD7AF00,
    0xD7AF5F,
    0xD7AF87,
    0xD7AFAF,
    0xD7AFD7,
    0xD7AFFF,
    //
    0xD7D700,
    0xD7D75F,
    0xD7D787,
    0xD7D7AF,
    0xD7D7D7,
    0xD7D7FF,
    //
    0xD7FF00,
    0xD7FF5F,
    0xD7FF87,
    0xD7FFAF,
    0xD7FFD7,
    0xD7FFFF,
    //
    0xFF0000,
    0xFF005F,
    0xFF0087,
    0xFF00AF,
    0xFF00D7,
    0xFF00FF,
    //
    0xFF5F00,
    0xFF5F5F,
    0xFF5F87,
    0xFF5FAF,
    0xFF5FD7,
    0xFF5FFF,
    //
    0xFF8700,
    0xFF875F,
    0xFF8787,
    0xFF87AF,
    0xFF87D7,
    0xFF87FF,
    //
    0xFFAF00,
    0xFFAF5F,
    0xFFAF87,
    0xFFAFAF,
    0xFFAFD7,
    0xFFAFFF,
    //
    0xFFD700,
    0xFFD75F,
    0xFFD787,
    0xFFD7AF,
    0xFFD7D7,
    0xFFD7FF,
    //
    0xFFFF00,
    0xFFFF5F,
    0xFFFF87,
    0xFFFFAF,
    0xFFFFD7,
    0xFFFFFF,
    //
    0x080808,
    0x121212,
    0x1C1C1C,
    0x262626,
    0x303030,
    0x3A3A3A,
    0x444444,
    0x4E4E4E,
    0x585858,
    0x626262,
    0x6C6C6C,
    0x767676,
    0x808080,
    0x8A8A8A,
    0x949494,
    0x9E9E9E,
    0xA8A8A8,
    0xB2B2B2,
    0xBCBCBC,
    0xC6C6C6,
    0xD0D0D0,
    0xDADADA,
    0xE4E4E4,
    0xEEE8D5,
};

static SDL_Color make_color(uint32_t c) {
    SDL_Color color;
    color.r = (c & 0xFF0000) >> 16;
    color.g = (c & 0x00FF00) >> 8;
    color.b = (c & 0x0000FF);
    color.a = 0xFF;
    return color;
}

static bool poll_event(SDL_Event* data) {
    ZoneScoped;
    return SDL_PollEvent(data);
}

static bool poll_event_callback(void* data) {
    return poll_event((SDL_Event*)data);
}

static void process_event(Server* server, Client* client, SDL_Event event) {
    ZoneScoped;

    switch (event.type) {
    case SDL_QUIT:
        client->queue_quit = true;
        break;

    case SDL_TEXTINPUT:
        if (!(SDL_GetModState() & ~KMOD_SHIFT)) {
            for (char* p = event.text.text; *p; ++p) {
                Key key = {};
                key.code = *p;
                server->receive(client, key);
            }
        }
        break;

    case SDL_TEXTEDITING:
        printf("Todo: handle SDL_TEXTEDITING\n");
        // printf("Edit: text: %s, start: %" PRIi32 ", length: %" PRIi32 "\n", event.edit.text,
        //       event.edit.start, event.edit.length);
        break;

    case SDL_KEYDOWN: {
        Key key = {};
        switch (event.key.keysym.sym) {
        case SDLK_LALT:
        case SDLK_RALT:
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
        case SDLK_LGUI:
        case SDLK_RGUI:
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            // Ignore non-text button presses.  These will be part of the `mod` of the actual key.
            return;

#define KEY_CASE(name, value) \
    case SDLK_##name:         \
        key.code = value;     \
        break

            KEY_CASE(SPACE, ' ');
            KEY_CASE(BACKSPACE, 127);
            KEY_CASE(RETURN, '\n');
            KEY_CASE(TAB, '\t');

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

        if ((event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT)) == 0 && (isprint(key.code))) {
            // Ignore key presses
            return;
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
                if (isalpha(key.code)) {
                    // It's already upper case
                } else {
                    key.modifiers |= SHIFT;
                }
                break;
            }
        } else {
            key.code = tolower(key.code);
        }

        server->receive(client, key);
    } break;
    }
}

static void process_events(Server* server, Client* client) {
    ZoneScoped;

    SDL_Event event;
    while (poll_event(&event)) {
        process_event(server, client, event);
        if (client->queue_quit) {
            return;
        }
    }
}

static SDL_Texture* render_text_to_texture(SDL_Renderer* renderer,
                                           TTF_Font* font,
                                           const char* text,
                                           SDL_Color color) {
    ZoneScoped;

    SDL_Surface* surface = TTF_RenderText_Blended(font, text, color);
    if (!surface) {
        fprintf(stderr, "Failed to render text '%s': %s\n", text, SDL_GetError());
        return nullptr;
    }
    CZ_DEFER(SDL_FreeSurface(surface));

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        fprintf(stderr, "Failed to create a texture: %s\n", SDL_GetError());
    }
    return texture;
}

static void render_texture(SDL_Renderer* renderer, SDL_Texture* texture, int x, int y) {
    ZoneScoped;

    SDL_Rect dest;
    dest.x = x;
    dest.y = y;
    SDL_QueryTexture(texture, nullptr, nullptr, &dest.w, &dest.h);
    SDL_RenderCopy(renderer, texture, nullptr, &dest);
}

static void render(SDL_Window* window,
                   SDL_Renderer* renderer,
                   TTF_Font* font,
                   int* total_rows,
                   int* total_cols,
                   int cwidth,
                   int cheight,
                   Cell** cellss,
                   Window_Cache** window_cache,
                   Editor* editor,
                   Client* client) {
    ZoneScoped;
    FrameMarkStart("sdl");

    int rows, cols;
    {
        ZoneScopedN("detect resize");
        SDL_GetWindowSize(window, &cols, &rows);
        cols /= cwidth;
        rows /= cheight;

        if (rows != *total_rows || cols != *total_cols) {
            SDL_Color bgc = make_color(colors[0]);
            SDL_SetRenderDrawColor(renderer, bgc.r, bgc.g, bgc.b, bgc.a);
            SDL_RenderClear(renderer);

            destroy_window_cache(*window_cache);
            *window_cache = nullptr;

            free(cellss[0]);
            free(cellss[1]);

            *total_rows = rows;
            *total_cols = cols;

            size_t grid_size = rows * cols;
            cellss[0] = (Cell*)malloc(grid_size * sizeof(Cell));
            cellss[1] = (Cell*)malloc(grid_size * sizeof(Cell));

            for (size_t i = 0; i < grid_size; ++i) {
                cellss[0][i].face = {};
                cellss[0][i].code = ' ';
                cellss[1][i].face = {};
                cellss[1][i].code = ' ';
            }
        }
    }

    render_to_cells(cellss[1], window_cache, rows, cols, editor, client);

    {
        ZoneScopedN("blit cells");

        char buffer[2] = {};
        buffer[0] = 0;

        int index = 0;
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                ZoneScopedN("check cell");

                Cell* new_cell = &cellss[1][index];
                if (cellss[0][index] != *new_cell) {
                    SDL_Rect rect;
                    SDL_Color bgc;
                    SDL_Color fgc;

                    {
                        ZoneScopedN("prepare render cell");
                        rect.x = x * cwidth;
                        rect.y = y * cheight;
                        rect.w = cwidth;
                        rect.h = cheight;

                        int style = 0;
                        if (new_cell->face.flags & Face::UNDERSCORE) {
                            style |= TTF_STYLE_UNDERLINE;
                        }
                        if (new_cell->face.flags & Face::BOLD) {
                            style |= TTF_STYLE_BOLD;
                        }
                        if (new_cell->face.flags & Face::ITALICS) {
                            style |= TTF_STYLE_ITALIC;
                        }
                        if (style == 0) {
                            style = TTF_STYLE_NORMAL;
                        }
                        TTF_SetFontStyle(font, style);

                        int16_t bg = new_cell->face.background;
                        int16_t max_colors = sizeof(colors) / sizeof(colors[0]);
                        if (bg < 0 || bg >= max_colors) {
                            bg = 0;
                        }
                        int16_t fg = new_cell->face.foreground;
                        if (fg < 0 || fg >= max_colors) {
                            fg = 7;
                        }

                        if (new_cell->face.flags & Face::REVERSE) {
                            cz::swap(fg, bg);
                        }

                        bgc = make_color(colors[bg]);
                        fgc = make_color(colors[fg]);
                    }

                    {
                        ZoneScopedN("render cell background");
                        SDL_SetRenderDrawColor(renderer, bgc.r, bgc.g, bgc.b, bgc.a);
                        SDL_RenderFillRect(renderer, &rect);
                    }

                    buffer[0] = new_cell->code;

                    SDL_Texture* rendered_char =
                        render_text_to_texture(renderer, font, buffer, fgc);
                    if (!rendered_char) {
                        continue;
                    }
                    CZ_DEFER(SDL_DestroyTexture(rendered_char));
                    render_texture(renderer, rendered_char, rect.x, rect.y);
                }
                ++index;
            }
        }
    }

    cz::swap(cellss[0], cellss[1]);

    {
        ZoneScopedN("present");
        SDL_RenderPresent(renderer);
    }

    FrameMarkEnd("sdl");
}

static bool get_c_dims(TTF_Font* font, int* cwidth, int* cheight) {
    SDL_Surface* surface = TTF_RenderText_Blended(font, "c", {0x00, 0x00, 0x00, 0xff});
    if (!surface) {
        fprintf(stderr, "Failed to render any text: %s\n", SDL_GetError());
        return false;
    }
    CZ_DEFER(SDL_FreeSurface(surface));

    *cwidth = surface->w;
    *cheight = surface->h;

    return true;
}

static void set_clipboard_variable(cz::String* clipboard, cz::Str text) {
    clipboard->set_len(0);
    clipboard->reserve(cz::heap_allocator(), text.len + 1);
    clipboard->append(text);
    clipboard->null_terminate();
}

static void process_clipboard_updates(Server* server, Client* client, cz::String* clipboard) {
    const char* clipboard_currently_cstr = SDL_GetClipboardText();
    if (clipboard_currently_cstr) {
        cz::Str clipboard_currently = clipboard_currently_cstr;
        if (*clipboard != clipboard_currently) {
            set_clipboard_variable(clipboard, clipboard_currently);

            Copy_Chain* chain = server->editor.copy_buffer.allocator().alloc<Copy_Chain>();
            chain->value.init_duplicate(server->editor.copy_buffer.allocator(), *clipboard);
            chain->previous = client->global_copy_chain;
            client->global_copy_chain = chain;
        }
    }
}

static int sdl_copy(void* data, cz::Str text) {
    cz::String* clipboard = (cz::String*)data;
    set_clipboard_variable(clipboard, text);
    return SDL_SetClipboardText(clipboard->buffer());
}

void run(Server* server, Client* client) {
    ZoneScoped;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return;
    }
    CZ_DEFER(SDL_Quit());

    if (TTF_Init() != 0) {
        fprintf(stderr, "Failed to initialize TTF: %s\n", SDL_GetError());
        return;
    }
    CZ_DEFER(TTF_Quit());

    SDL_Window* window = SDL_CreateWindow("Mag", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          800, 800, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "Failed to create a window: %s\n", SDL_GetError());
        return;
    }
    CZ_DEFER(SDL_DestroyWindow(window));

    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "Failed to create a renderer: %s\n", SDL_GetError());
        return;
    }
    CZ_DEFER(SDL_DestroyRenderer(renderer));

    const char* font_file;
#ifdef _WIN32
    font_file = "C:/Windows/Fonts/MesloLGM-Regular.ttf";
#else
    font_file = "/usr/share/fonts/TTF/MesloLGMDZ-Regular.ttf";
#endif
    TTF_Font* font = TTF_OpenFont(font_file, 15);
    if (!font) {
        fprintf(stderr, "Failed to open the font file '%s': %s\n", font_file, SDL_GetError());
        return;
    }
    CZ_DEFER(TTF_CloseFont(font));

    Cell* cellss[2] = {nullptr, nullptr};
    CZ_DEFER({
        free(cellss[0]);
        free(cellss[1]);
    });

    Window_Cache* window_cache = nullptr;
    CZ_DEFER(destroy_window_cache(window_cache));

    int total_rows = 0;
    int total_cols = 0;

    int cwidth;
    int cheight;
    if (!get_c_dims(font, &cwidth, &cheight)) {
        return;
    }

    SDL_StartTextInput();
    CZ_DEFER(SDL_StopTextInput());

    const float MAX_FRAMES = 60.0f;
    const float FRAME_LENGTH = 1000.0f / MAX_FRAMES;

    cz::String clipboard = {};
    CZ_DEFER(clipboard.drop(cz::heap_allocator()));

    client->system_copy_text_func = sdl_copy;
    client->system_copy_text_data = &clipboard;

    while (1) {
        ZoneScopedN("sdl main loop");

        Uint32 frame_start_ticks = SDL_GetTicks();

        render(window, renderer, font, &total_rows, &total_cols, cwidth, cheight, cellss,
               &window_cache, &server->editor, client);

        if (client->mini_buffer_completion_cache.state != Completion_Cache::LOADED &&
            client->_message.tag > Message::SHOW) {
            CZ_DEBUG_ASSERT(server->editor.theme.completion_filter != nullptr);
            server->editor.theme.completion_filter(
                &client->mini_buffer_completion_cache.filter_context,
                client->mini_buffer_completion_cache.engine, &server->editor,
                &client->mini_buffer_completion_cache.engine_context);
            client->mini_buffer_completion_cache.state = Completion_Cache::LOADED;
            continue;
        }

        server->editor.tick_jobs();

        SDL_Event event;
        if (cache_windows_check_points(window_cache, client->window, &server->editor,
                                       poll_event_callback, &event)) {
            process_event(server, client, event);
        }

        process_events(server, client);

        process_clipboard_updates(server, client, &clipboard);

        if (client->queue_quit) {
            break;
        }

        Uint32 frame_end_ticks = SDL_GetTicks();
        Uint32 elapsed_ticks = frame_end_ticks - frame_start_ticks;
        if (elapsed_ticks < FRAME_LENGTH) {
            SDL_Delay((Uint32)floor(FRAME_LENGTH - elapsed_ticks));
        }
    }
}

}
}
}
