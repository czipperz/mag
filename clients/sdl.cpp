#include "sdl.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_syswm.h>
#include <SDL_ttf.h>
#include <ctype.h>
#include <inttypes.h>
#include <Tracy.hpp>
#include <command_macros.hpp>
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

static bool poll_event_callback(void* data) {
    return poll_event((SDL_Event*)data);
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
            KEY_CASE(RETURN, '\n');
            KEY_CASE(TAB, '\t');
            KEY_CASE(UP, Key_Code::UP);
            KEY_CASE(DOWN, Key_Code::DOWN);
            KEY_CASE(LEFT, Key_Code::LEFT);
            KEY_CASE(RIGHT, Key_Code::RIGHT);

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
            (key.code <= UCHAR_MAX && isprint(key.code))) {
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
                if (key.code <= UCHAR_MAX && isalpha(key.code)) {
                    // It's already upper case
                } else {
                    key.modifiers |= SHIFT;
                }
                break;
            }
        } else {
            if (key.code <= UCHAR_MAX) {
                key.code = tolower(key.code);
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
            mouse->mouse_pos = spq.sp;
            if (mouse->mouse_down && spq.sp.found_window && spq.sp.found_position &&
                mouse->mouse_down_pos.found_window && mouse->mouse_down_pos.found_position &&
                spq.sp.window == mouse->mouse_down_pos.window) {
                if (spq.sp.window->show_marks ||
                    spq.sp.position != mouse->mouse_down_pos.position) {
                    spq.sp.window->show_marks = true;
                    spq.sp.window->cursors[0].mark = mouse->mouse_down_pos.position;
                    spq.sp.window->cursors[0].point = spq.sp.position;
                }
            }
        } else if (spq.data == MOUSE_DOWN) {
            mouse->mouse_down = true;
            mouse->mouse_down_pos = spq.sp;

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
            mouse->mouse_down = false;

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
            mouse->mouse_down = false;

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

static void render(SDL_Window* window,
                   SDL_Renderer* renderer,
                   TTF_Font* font,
                   SDL_Texture** texture,
                   SDL_Surface** surface,
                   int* total_rows,
                   int* total_cols,
                   int character_width,
                   int character_height,
                   Cell** cellss,
                   Window_Cache** window_cache,
                   Editor* editor,
                   Client* client,
                   cz::Slice<Screen_Position_Query> spqs) {
    ZoneScoped;
    FrameMarkStart("sdl");

    int rows, cols;
    {
        ZoneScopedN("detect resize");
        int width, height;
        SDL_GetWindowSize(window, &width, &height);
        cols = width / character_width;
        rows = height / character_height;

        if (rows != *total_rows || cols != *total_cols) {
            if (*surface) {
                SDL_FreeSurface(*surface);
            }
            *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
            if (*texture) {
                SDL_DestroyTexture(*texture);
            }
            *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING, width, height);

            SDL_Color bgc = make_color(editor->theme.colors, 0);
            SDL_FillRect(*surface, nullptr,
                         SDL_MapRGBA((*surface)->format, bgc.r, bgc.g, bgc.b, bgc.a));

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

    render_to_cells(cellss[1], window_cache, rows, cols, editor, client, spqs);

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
                        rect.x = x * character_width;
                        rect.y = y * character_height;
                        rect.w = character_width;
                        rect.h = character_height;

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

                        Face_Color bg =
                            get_face_color_or(editor->theme.colors, new_cell->face.background, 0);
                        Face_Color fg =
                            get_face_color_or(editor->theme.colors, new_cell->face.foreground, 7);

                        if (new_cell->face.flags & Face::REVERSE) {
                            cz::swap(fg, bg);
                        }

                        bgc = make_color(editor->theme.colors, bg);
                        fgc = make_color(editor->theme.colors, fg);
                    }

                    {
                        ZoneScopedN("render cell background");
                        SDL_FillRect(*surface, &rect,
                                     SDL_MapRGBA((*surface)->format, bgc.r, bgc.g, bgc.b, bgc.a));
                    }

                    buffer[0] = new_cell->code;

                    SDL_Surface* rendered_char;
                    {
                        ZoneScopedN("TTF_RenderText_Blended");
                        rendered_char = TTF_RenderText_Blended(font, buffer, fgc);
                    }
                    if (!rendered_char) {
                        fprintf(stderr, "Failed to render text '%s': %s\n", buffer, TTF_GetError());
                        continue;
                    }
                    CZ_DEFER(SDL_FreeSurface(rendered_char));

                    {
                        ZoneScopedN("SDL_BlitSurface");
                        SDL_BlitSurface(rendered_char, nullptr, *surface, &rect);
                    }
                }
                ++index;
            }
        }
    }

    cz::swap(cellss[0], cellss[1]);

    {
        ZoneScopedN("present");
        void* pixels;
        int pitch;

        SDL_LockTexture(*texture, nullptr, &pixels, &pitch);
        SDL_LockSurface(*surface);
        CZ_DEBUG_ASSERT(pitch == (*surface)->pitch);
        memcpy(pixels, (*surface)->pixels, (*surface)->h * (*surface)->pitch);
        SDL_UnlockSurface(*surface);
        SDL_UnlockTexture(*texture);

        SDL_RenderCopy(renderer, *texture, nullptr, nullptr);

        SDL_RenderPresent(renderer);
    }

    FrameMarkEnd("sdl");
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

static void process_clipboard_updates(Server* server,
                                      Client* client,
                                      Clipboard_Context* clipboard) {
    ZoneScoped;

    char* clipboard_currently_cstr = SDL_GetClipboardText();
    if (clipboard_currently_cstr) {
        cz::Str clipboard_currently = clipboard_currently_cstr;

#ifdef _WIN32
        if (clipboard_currently == "") {
            return;
        }
#endif

#ifdef _WIN32
        cz::strip_carriage_returns(clipboard_currently_cstr, &clipboard_currently.len);
#endif

        if (clipboard->value != clipboard_currently) {
            set_clipboard_variable(&clipboard->value, clipboard_currently);

            Copy_Chain* chain = server->editor.copy_buffer.allocator().alloc<Copy_Chain>();
            chain->value =
                SSOStr::as_duplicate(server->editor.copy_buffer.allocator(), clipboard->value);
            chain->previous = client->global_copy_chain;
            client->global_copy_chain = chain;
        }

        SDL_free(clipboard_currently_cstr);
    }
}

static int sdl_copy(void* data, cz::Str text) {
    Clipboard_Context* clipboard = (Clipboard_Context*)data;
    set_clipboard_variable(&clipboard->value, text);
    return SDL_SetClipboardText(clipboard->value.buffer());
}

void setIcon(SDL_Window* sdlWindow) {
#ifdef _WIN32
    SDL_SysWMinfo wminfo;
    SDL_VERSION(&wminfo.version);
    if (SDL_GetWindowWMInfo(sdlWindow, &wminfo) == 1) {
        HWND hwnd = wminfo.info.win.window;

        HINSTANCE handle = ::GetModuleHandle(nullptr);
        HICON icon = ::LoadIcon(handle, "IDI_MAIN_ICON");
        if (icon != nullptr) {
            ::SetClassLongPtr(hwnd, GCLP_HICON, reinterpret_cast<LONG>(icon));
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
        SDL_SetWindowIcon(sdlWindow, icon);
        SDL_FreeSurface(icon);
    }
#endif
}

static void switch_to_the_home_directory() {
    // Go home only we are in the program directory or we can't identify ourselves.
    cz::String wd = {};
    CZ_DEFER(wd.drop(cz::heap_allocator()));
    if (cz::get_working_directory(cz::heap_allocator(), &wd).is_ok()) {
        cz::Str prog_dir = program_dir;
        // Get rid of the trailing / as it isn't in the working directory.
        prog_dir.len--;
        if (prog_dir != wd) {
            return;
        }
    }

    const char* user_home_path;
#ifdef _WIN32
    user_home_path = getenv("USERPROFILE");
#else
    user_home_path = getenv("HOME");
#endif

    printf("Going home: %s\n", user_home_path);

    if (user_home_path) {
        cz::set_working_directory(user_home_path);
    }
}

void run(Server* server, Client* client) {
    ZoneScoped;

    switch_to_the_home_directory();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return;
    }
    CZ_DEFER(SDL_Quit());

    if (TTF_Init() != 0) {
        fprintf(stderr, "Failed to initialize TTF: %s\n", TTF_GetError());
        return;
    }
    CZ_DEFER(TTF_Quit());

    int img_init_flags = IMG_INIT_PNG;
    if (IMG_Init(img_init_flags) != img_init_flags) {
        fprintf(stderr, "Failed to initialize IMG: %s\n", IMG_GetError());
        return;
    }
    CZ_DEFER(IMG_Quit());

    SDL_Window* window = SDL_CreateWindow("Mag", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          800, 800, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "Failed to create a window: %s\n", SDL_GetError());
        return;
    }
    CZ_DEFER(SDL_DestroyWindow(window));

    setIcon(window);

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

    int character_width;
    int character_height;
    if (!get_character_dims(font, &character_width, &character_height)) {
        return;
    }

    Scroll_State scroll = {};

    SDL_StartTextInput();
    CZ_DEFER(SDL_StopTextInput());

    const float MAX_FRAMES = 60.0f;
    const float FRAME_LENGTH = 1000.0f / MAX_FRAMES;

    Clipboard_Context clipboard = {};
    CZ_DEFER(clipboard.value.drop(cz::heap_allocator()));

    client->system_copy_text_func = sdl_copy;
    client->system_copy_text_data = &clipboard;

    Mouse_State mouse = {};
    CZ_DEFER(mouse.sp_queries.drop(cz::heap_allocator()));

    SDL_Surface* surface = nullptr;
    SDL_Texture* texture = nullptr;
    CZ_DEFER(if (surface) SDL_FreeSurface(surface));
    CZ_DEFER(if (texture) SDL_DestroyTexture(texture));

    while (1) {
        ZoneScopedN("sdl main loop");

        Uint32 frame_start_ticks = SDL_GetTicks();

        client->update_mini_buffer_completion_cache(&server->editor);
        load_mini_buffer_completion_cache(server, client);

        render(window, renderer, font, &texture, &surface, &total_rows, &total_cols,
               character_width, character_height, cellss, &window_cache, &server->editor, client,
               mouse.sp_queries);

        process_mouse_events(&server->editor, client, &mouse);

        server->editor.tick_jobs();

        SDL_Event event;
        if (cache_windows_check_points(window_cache, client->window, &server->editor,
                                       poll_event_callback, &event)) {
            process_event(server, client, event, &mouse, character_width, character_height);
        }

        process_events(server, client, &mouse, character_width, character_height);

        process_scroll(server, client, &mouse.scroll);

        process_clipboard_updates(server, client, &clipboard);

        process_buffer_external_updates(&server->editor, client, client->window);

        if (client->queue_quit) {
            break;
        }

        Uint32 frame_end_ticks = SDL_GetTicks();
        Uint32 elapsed_ticks = frame_end_ticks - frame_start_ticks;
        if (elapsed_ticks < FRAME_LENGTH) {
            ZoneScopedN("SDL_Delay");
            SDL_Delay((Uint32)floorf(FRAME_LENGTH - elapsed_ticks));
        }
    }
}

}
}
}
