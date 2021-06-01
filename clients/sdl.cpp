#include "sdl.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_syswm.h>
#include <SDL_ttf.h>
#include <inttypes.h>
#include <Tracy.hpp>
#include <command_macros.hpp>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/path.hpp>
#include <cz/process.hpp>
#include <cz/str.hpp>
#include <cz/working_directory.hpp>
#include "basic/copy_commands.hpp"
#include "cell.hpp"
#include "client.hpp"
#include "command_macros.hpp"
#include "file.hpp"
#include "movement.hpp"
#include "program_info.hpp"
#include "render.hpp"
#include "server.hpp"
#include "window_cache.hpp"

#ifdef _WIN32
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

static Face_Color get_face_color_or(uint32_t* colors, Face_Color fc, int16_t deflt) {
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

enum {
    MOUSE_MOVE,
    MOUSE_DOWN,
    MOUSE_SELECT_WORD,
    MOUSE_SELECT_LINE,
    MOUSE_UP,
    MOUSE_RIGHT,
};

struct Mouse_State {
    Scroll_State scroll;

    cz::Vector<Screen_Position_Query> sp_queries;

    uint8_t multi_click_counter = 0;
    uint8_t multi_click_offset = 0;
    Screen_Position mouse_pos;

    bool mouse_down;
    Screen_Position mouse_down_pos;
    int mouse_down_data;
};

static void process_event(Server* server,
                          Client* client,
                          SDL_Event event,
                          Mouse_State* mouse,
                          int character_width,
                          int character_height) {
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
        if (event.edit.length == 0) {
            break;
        }

        printf("Todo: handle SDL_TEXTEDITING\n");
        printf("Edit: text: %s, start: %" PRIi32 ", length: %" PRIi32 "\n", event.edit.text,
               event.edit.start, event.edit.length);

        break;

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
        if (mouse->mouse_pos.found_window) {
            client->selected_normal_window = mouse->mouse_pos.window;
        }

        bool mini = client->_select_mini_buffer;
        CZ_DEFER(client->_select_mini_buffer = mini);
        client->_select_mini_buffer = false;

        Key key = {};
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
            if (mouse->scroll.scrolling == 0) {
                mouse->scroll.y = 0;
            } else {
                double dy = event.mgesture.y - mouse->scroll.prev_pos;
                mouse->scroll.acceleration = dy * 40;
            }

            mouse->scroll.prev_pos = event.mgesture.y;
            mouse->scroll.scrolling = 1;
        }
        break;
    }

    case SDL_FINGERDOWN: {
        mouse->scroll.scrolling = 0;
        break;
    }

    case SDL_MOUSEMOTION: {
        Screen_Position_Query spq;
        spq.in_x = event.motion.x / character_width;
        spq.in_y = event.motion.y / character_height;
        spq.data = MOUSE_MOVE;
        mouse->sp_queries.reserve(cz::heap_allocator(), 1);
        mouse->sp_queries.push(spq);

        mouse->multi_click_offset = mouse->multi_click_counter;
        break;
    }

    case SDL_MOUSEBUTTONDOWN: {
        if (event.button.button == SDL_BUTTON_LEFT) {
            Screen_Position_Query spq;
            spq.in_x = event.button.x / character_width;
            spq.in_y = event.button.y / character_height;

            uint8_t clicks = event.button.clicks;
            if (clicks == 1) {
                mouse->multi_click_counter = 0;
                mouse->multi_click_offset = 0;
            }
            clicks -= mouse->multi_click_offset;

            if (clicks % 3 == 1) {
                spq.data = MOUSE_DOWN;
            } else if (clicks % 3 == 2) {
                spq.data = MOUSE_SELECT_WORD;
            } else {
                spq.data = MOUSE_SELECT_LINE;
            }
            ++mouse->multi_click_counter;

            mouse->sp_queries.reserve(cz::heap_allocator(), 1);
            mouse->sp_queries.push(spq);
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            Screen_Position_Query spq;
            spq.in_x = event.button.x / character_width;
            spq.in_y = event.button.y / character_height;
            spq.data = MOUSE_RIGHT;
            mouse->sp_queries.reserve(cz::heap_allocator(), 1);
            mouse->sp_queries.push(spq);
        } else if (event.button.button == SDL_BUTTON_MIDDLE) {
            Key key;
            key.modifiers = 0;
            key.code = Key_Code::MOUSE3;
            server->receive(client, key);
        } else if (event.button.button == SDL_BUTTON_X1) {
            Key key;
            key.modifiers = 0;
            key.code = Key_Code::MOUSE4;
            server->receive(client, key);
        } else if (event.button.button == SDL_BUTTON_X2) {
            Key key;
            key.modifiers = 0;
            key.code = Key_Code::MOUSE5;
            server->receive(client, key);
        }
        break;
    }

    case SDL_MOUSEBUTTONUP: {
        if (event.button.button == SDL_BUTTON_LEFT) {
            Screen_Position_Query spq;
            spq.in_x = event.button.x / character_width;
            spq.in_y = event.button.y / character_height;
            spq.data = MOUSE_UP;
            mouse->sp_queries.reserve(cz::heap_allocator(), 1);
            mouse->sp_queries.push(spq);
        }
        break;
    }

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

        if ((event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT)) == 0 &&
            (key.code <= UCHAR_MAX && cz::is_print(key.code))) {
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
                if (key.code <= UCHAR_MAX && cz::is_alpha(key.code)) {
                    // It's already upper case
                } else {
                    key.modifiers |= SHIFT;
                }
                break;
            }
        } else {
            if (key.code <= UCHAR_MAX) {
                key.code = cz::to_lower(key.code);
            }
        }

        server->receive(client, key);
    } break;
    }
}

static void process_events(Server* server,
                           Client* client,
                           Mouse_State* mouse,
                           int character_width,
                           int character_height) {
    ZoneScoped;

    SDL_Event event;
    while (poll_event(&event)) {
        process_event(server, client, event, mouse, character_width, character_height);
        if (client->queue_quit) {
            return;
        }
    }
}

static void process_scroll(Server* server, Client* client, Scroll_State* scroll) {
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

void process_mouse_events(Editor* editor, Client* client, Mouse_State* mouse) {
    cz::Slice<Screen_Position_Query> spqs = mouse->sp_queries;
    mouse->sp_queries.set_len(0);

    for (size_t spqi = 0; spqi < spqs.len; ++spqi) {
        Screen_Position_Query spq = spqs[spqi];
        if (spq.data == MOUSE_MOVE) {
            client->mouse.window = spq.sp.window;
            client->mouse.row = spq.sp.window_row;
            client->mouse.column = spq.sp.window_column;

            mouse->mouse_pos = spq.sp;
            if (mouse->mouse_down && spq.sp.found_window && spq.sp.found_position &&
                mouse->mouse_down_pos.found_window && mouse->mouse_down_pos.found_position &&
                spq.sp.window == mouse->mouse_down_pos.window) {
                if (spq.sp.window->show_marks ||
                    spq.sp.position != mouse->mouse_down_pos.position) {
                    kill_extra_cursors(spq.sp.window, client);
                    if (mouse->mouse_down_data == MOUSE_DOWN) {
                        spq.sp.window->show_marks = true;
                        spq.sp.window->cursors[0].mark = mouse->mouse_down_pos.position;
                        spq.sp.window->cursors[0].point = spq.sp.position;
                    } else {
                        WITH_WINDOW_BUFFER(spq.sp.window);
                        Contents_Iterator mark =
                            buffer->contents.iterator_at(mouse->mouse_down_pos.position);
                        Contents_Iterator point = buffer->contents.iterator_at(spq.sp.position);

                        Contents_Iterator* start;
                        Contents_Iterator* end;
                        if (mark.position < point.position) {
                            start = &mark;
                            end = &point;
                        } else {
                            start = &point;
                            end = &mark;
                        }

                        if (mouse->mouse_down_data == MOUSE_SELECT_WORD) {
                            forward_char(start);
                            backward_word(start);
                            forward_word(end);
                        } else {
                            start_of_line(start);
                            end_of_line(end);
                            forward_char(end);
                        }

                        spq.sp.window->show_marks = true;
                        spq.sp.window->cursors[0].mark = mark.position;
                        spq.sp.window->cursors[0].point = point.position;
                    }
                }
            }
        } else if (spq.data == MOUSE_DOWN) {
            mouse->mouse_down = true;
            mouse->mouse_down_pos = spq.sp;
            mouse->mouse_down_data = spq.data;

            if (spq.sp.found_window) {
                Window_Unified* window = spq.sp.window;
                client->selected_normal_window = window;

                if (spq.sp.found_position) {
                    kill_extra_cursors(window, client);
                    window->show_marks = false;
                    window->cursors[0].point = spq.sp.position;
                }
            }
        } else if (spq.data == MOUSE_SELECT_WORD) {
            mouse->mouse_down = true;
            mouse->mouse_down_data = spq.data;

            if (spq.sp.found_window) {
                Window_Unified* window = spq.sp.window;
                client->selected_normal_window = window;

                if (spq.sp.found_position) {
                    WITH_WINDOW_BUFFER(window);
                    Contents_Iterator start = buffer->contents.iterator_at(spq.sp.position);
                    Contents_Iterator end = start;
                    forward_char(&start);
                    backward_word(&start);
                    forward_word(&end);

                    kill_extra_cursors(window, client);
                    window->show_marks = true;
                    window->cursors[0].mark = start.position;
                    window->cursors[0].point = end.position;
                }
            }
        } else if (spq.data == MOUSE_SELECT_LINE) {
            mouse->mouse_down = true;
            mouse->mouse_down_data = spq.data;

            if (spq.sp.found_window) {
                Window_Unified* window = spq.sp.window;
                client->selected_normal_window = window;

                if (spq.sp.found_position) {
                    WITH_WINDOW_BUFFER(window);
                    Contents_Iterator start = buffer->contents.iterator_at(spq.sp.position);
                    Contents_Iterator end = start;
                    start_of_line(&start);
                    end_of_line(&end);
                    forward_char(&end);

                    kill_extra_cursors(window, client);
                    window->show_marks = true;
                    window->cursors[0].mark = start.position;
                    window->cursors[0].point = end.position;
                }
            }
        } else if (spq.data == MOUSE_UP) {
            mouse->mouse_down = false;
        } else if (spq.data == MOUSE_RIGHT) {
            if (spq.sp.found_window) {
                Window_Unified* window = spq.sp.window;
                client->selected_normal_window = window;

                bool mini = client->_select_mini_buffer;
                CZ_DEFER(client->_select_mini_buffer = mini);
                client->_select_mini_buffer = false;

                Command_Source source = {};
                source.client = client;
                if (window->show_marks) {
                    basic::command_copy(editor, source);
                    window->show_marks = false;
                } else {
                    basic::command_paste(editor, source);
                }
            }
        }
    }
}

static void blit_surface(SDL_Surface* surface, SDL_Surface* rendered_char, SDL_Rect* rect) {
    ZoneScopedN("SDL_BlitSurface");
    SDL_BlitSurface(rendered_char, nullptr, surface, rect);
}

static void render(SDL_Window* window,
                   SDL_Renderer* renderer,
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
                   Editor* editor,
                   Client* client,
                   cz::Slice<Screen_Position_Query> spqs,
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

    render_to_cells(cellss[1], window_cache, rows, cols, editor, client, spqs);

    bool any_changes = false;

    {
        ZoneScopedN("draw cells");

        for (int y = 0, index = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x, ++index) {
                Cell* new_cell = &cellss[1][index];
                if (!force_redraw && cellss[0][index] == *new_cell &&
                    (x == 0 || cellss[0][index - 1] == cellss[1][index - 1]) &&
                    (x + 1 == cols || cellss[0][index + 1] == cellss[1][index + 1])) {
                    continue;
                }

                any_changes = true;

                ZoneScopedN("draw cell background");

                Face_Color bg = (new_cell->face.flags & Face::REVERSE) ? new_cell->face.foreground
                                                                       : new_cell->face.background;
                bg = get_face_color_or(editor->theme.colors, bg,
                                       (new_cell->face.flags & Face::REVERSE) ? 7 : 0);

                SDL_Rect rect;
                rect.x = x * character_width;
                rect.y = y * character_height;
                rect.w = character_width;
                rect.h = character_height;

                SDL_Color bgc = make_color(editor->theme.colors, bg);

                {
                    ZoneScopedN("SDL_FillRect");
                    SDL_FillRect(surface, &rect,
                                 SDL_MapRGBA(surface->format, bgc.r, bgc.g, bgc.b, bgc.a));
                }
            }
        }

        for (int y = 0, index = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x, ++index) {
                Cell* new_cell = &cellss[1][index];
                if (!force_redraw && cellss[0][index] == *new_cell &&
                    (x == 0 || cellss[0][index - 1] == cellss[1][index - 1]) &&
                    (x + 1 == cols || cellss[0][index + 1] == cellss[1][index + 1])) {
                    continue;
                }

                ZoneScopedN("draw cell foreground");

                Face_Color fg = (new_cell->face.flags & Face::REVERSE) ? new_cell->face.background
                                                                       : new_cell->face.foreground;
                fg = get_face_color_or(editor->theme.colors, fg,
                                       (new_cell->face.flags & Face::REVERSE) ? 0 : 7);

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

static void set_clipboard_variable(cz::String* clipboard, cz::Str text) {
    clipboard->set_len(0);
    clipboard->reserve(cz::heap_allocator(), text.len + 1);
    clipboard->append(text);
    clipboard->null_terminate();
}

struct Clipboard_Context {
    cz::String value;
};

static int process_clipboard_updates(Editor* editor,
                                     Copy_Chain** global_copy_chain,
                                     Clipboard_Context* clipboard) {
    ZoneScoped;

    char* clipboard_currently_cstr = SDL_GetClipboardText();
    if (!clipboard_currently_cstr) {
        return -1;
    }

    CZ_DEFER(SDL_free(clipboard_currently_cstr));
    cz::Str clipboard_currently = clipboard_currently_cstr;

    if (clipboard_currently == "") {
        return -1;
    }

#ifdef _WIN32
    cz::strip_carriage_returns(clipboard_currently_cstr, &clipboard_currently.len);
#endif

    if (clipboard->value != clipboard_currently) {
        set_clipboard_variable(&clipboard->value, clipboard_currently);

        Copy_Chain* chain = editor->copy_buffer.allocator().alloc<Copy_Chain>();
        chain->value = SSOStr::as_duplicate(editor->copy_buffer.allocator(), clipboard->value);
        chain->previous = *global_copy_chain;
        *global_copy_chain = chain;
    }

    return 0;
}

struct Update_Global_Copy_Chain_Data {
    Editor* editor;
    Clipboard_Context clipboard;
};

static int sdl_update_global_copy_chain(Copy_Chain** global_copy_chain, void* _data) {
    Update_Global_Copy_Chain_Data* data = (Update_Global_Copy_Chain_Data*)_data;
    return process_clipboard_updates(data->editor, global_copy_chain, &data->clipboard);
}

static int sdl_copy(void* data, cz::Str text) {
    Clipboard_Context* clipboard = (Clipboard_Context*)data;
    set_clipboard_variable(&clipboard->value, text);
    return SDL_SetClipboardText(clipboard->value.buffer());
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
            ::SetClassLongPtr(hwnd, GCLP_HICON, (LONG)icon);
        }
    }
#else
    auto prog_name = cz::path::directory_component(program_name);
    if (!prog_name.is_present) {
        prog_name.value = "";
    }
    cz::String logo = {};
    logo.reserve(cz::heap_allocator(), prog_name.value.len + 8 + 1);
    CZ_DEFER(logo.drop(cz::heap_allocator()));
    logo.append(prog_name.value);
    logo.append("logo.png");
    logo.null_terminate();

    SDL_Surface* icon = IMG_Load(logo.buffer());
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
                      uint32_t new_font_size) {
    ZoneScoped;

    // Set font config variables even if we fail to load the font
    // so we don't keep looping over and over trying to load it.
    font_file->reserve(cz::heap_allocator(), new_font_file.len + 1);
    font_file->append(new_font_file);
    font_file->null_terminate();
    *font_size = new_font_size;

    // Load the font.
    TTF_Font* new_font = TTF_OpenFont(font_file->buffer(), *font_size);
    if (!new_font) {
        fprintf(stderr, "Failed to open the font file '%s': %s\n", font_file->buffer(),
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
    ZoneScoped;

    server->set_async_locked(false);
    // @Unlocked No usage of server or client can be made in this region!!!

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

    const char* window_name = "Mag";
#ifndef NDEBUG
    window_name = "Mag [DEBUG]";
#endif

    SDL_Window* window;
    {
        ZoneScopedN("SDL_CreateWindow");
        window = SDL_CreateWindow(window_name, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  800, 800, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    }
    if (!window) {
        fprintf(stderr, "Failed to create a window: %s\n", SDL_GetError());
        return;
    }
    CZ_DEFER(SDL_DestroyWindow(window));

    set_icon(window);

    SDL_Renderer* renderer;
    {
        ZoneScopedN("SDL_CreateRenderer");
        renderer =
            SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        // Retry with less requirements if it doesn't work the first time around.
        if (!renderer) {
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        }
        if (!renderer) {
            renderer = SDL_CreateRenderer(window, -1, 0);
        }
    }
    if (!renderer) {
        fprintf(stderr, "Failed to create a renderer: %s\n", SDL_GetError());
        return;
    }
    CZ_DEFER(SDL_DestroyRenderer(renderer));

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
                   surface_cache, server->editor.theme.font_file, server->editor.theme.font_size)) {
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

    SDL_StartTextInput();
    CZ_DEFER(SDL_StopTextInput());

    // If no redraws have happened in a long time then increase the frame length.
    uint32_t redrew_last;
    bool redrew = false;

    Update_Global_Copy_Chain_Data ugccd = {};
    ugccd.editor = &server->editor;
    CZ_DEFER(ugccd.clipboard.value.drop(cz::heap_allocator()));

    client->system_copy_text_func = sdl_copy;
    client->system_copy_text_data = &ugccd.clipboard;
    client->update_global_copy_chain_func = sdl_update_global_copy_chain;
    client->update_global_copy_chain_data = &ugccd;

    Mouse_State mouse = {};
    CZ_DEFER(mouse.sp_queries.drop(cz::heap_allocator()));

    while (1) {
        ZoneScopedN("sdl main loop");

        uint32_t frame_start_ticks = SDL_GetTicks();

        bool force_redraw = false;

        // If the font info was updated then reload the font.
        cz::Str new_font_file = server->editor.theme.font_file;
        if (font_file != new_font_file || font_size != server->editor.theme.font_size) {
            font_file.set_len(0);

            // If loading the font fails then we print a message inside `load_font()` and continue.
            if (load_font(&font_file, &font_size, &font, &character_width, &character_height,
                          surface_cache, server->editor.theme.font_file,
                          server->editor.theme.font_size)) {
                // Redraw the screen with the new font info.
                force_redraw = true;
            }
        }

        client->update_mini_buffer_completion_cache(&server->editor);
        load_mini_buffer_completion_cache(server, client);

        bool redrew_this_time = false;
        render(window, renderer, font, surface_cache, &old_width, &old_height, &total_rows,
               &total_cols, character_width, character_height, cellss, &window_cache,
               &server->editor, client, mouse.sp_queries, force_redraw, &redrew_this_time);

        process_mouse_events(&server->editor, client, &mouse);

        server->slurp_jobs();
        server->run_synchronous_jobs(client);

        process_events(server, client, &mouse, character_width, character_height);

        process_scroll(server, client, &mouse.scroll);

        process_buffer_external_updates(&server->editor, client, client->window);

        if (client->queue_quit) {
            break;
        }

        uint32_t frame_end_ticks = SDL_GetTicks();

        // 60 fps is default.
        uint32_t frame_length = (uint32_t)(1000.0f / 60.0f);
        if (redrew_this_time) {
            // Record that we redrew.
            redrew_last = frame_end_ticks;
            redrew = true;
        } else if (redrew_last + 10000 < frame_end_ticks) {
            // If ten seconds have elapsed then lower the frame rate to 8 fps.
            frame_length = (uint32_t)(1000.0f / 8.0f);
        }

        uint32_t elapsed_ticks = frame_end_ticks - frame_start_ticks;
        if (elapsed_ticks < frame_length) {
            ZoneScopedN("SDL_Delay");
            uint32_t sleep_time = frame_length - elapsed_ticks;
            ZoneValue(sleep_time);

            server->set_async_locked(false);
            SDL_Delay(sleep_time);
            server->set_async_locked(true);
        }

        FrameMark;
    }
}

}
}
}
