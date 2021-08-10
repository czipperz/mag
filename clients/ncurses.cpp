#ifdef HAS_NCURSES

#include "ncurses.hpp"

#include <ncurses.h>
#include <Tracy.hpp>
#include <algorithm>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <thread>
#include "cell.hpp"
#include "client.hpp"
#include "completion.hpp"
#include "custom/config.hpp"
#include "movement.hpp"
#include "render.hpp"
#include "server.hpp"
#include "token.hpp"
#include "window_cache.hpp"

using namespace mag::render;

namespace mag {
namespace client {
namespace ncurses {

#undef MOCK_INPUT

#ifdef MOCK_INPUT
static bool is_nodelay = false;
#define nodelay(WINDOW, VALUE) (is_nodelay = (VALUE))
#endif

static int mock_getch() {
    ZoneScoped;

#ifndef MOCK_INPUT
    return getch();
#else
    static int keys[] = {27, '>', 24, 3};
    static int times[] = {800, 800, 1100, 1200};
    static int index = 0;
    static auto time_start = std::chrono::steady_clock::now();

    if (index == sizeof(times) / sizeof(*times)) {
        return ERR;
    }

    auto duration = std::chrono::steady_clock::now() - time_start;
    auto time = std::chrono::milliseconds(times[index]);
    if (duration >= time) {
        return keys[index++];
    } else {
        if (is_nodelay) {
            return ERR;
        } else {
            std::this_thread::sleep_until(time_start + time);
            return keys[index++];
        }
    }
#endif
}

#undef getch
#define getch() (mock_getch())

static int16_t get_face_color(Face_Color fc) {
    if (fc.is_themed) {
        return fc.x.theme_index;
    } else {
        return -1;
    }
}

static int16_t get_face_color_or(Face_Color fc, int16_t deflt) {
    int16_t color = get_face_color(fc);
    if (color < 0 || color >= COLORS) {
        color = deflt;
    }
    return color;
}

static void render(int* total_rows,
                   int* total_cols,
                   Cell** cellss,
                   Window_Cache** window_cache,
                   Window_Cache** mini_buffer_window_cache,
                   Editor* editor,
                   Client* client,
                   int16_t* colors,
                   int16_t used_colors) {
    ZoneScoped;

    int rows, cols;
    {
        ZoneScopedN("detect resize");
        getmaxyx(stdscr, rows, cols);

        if (rows != *total_rows || cols != *total_cols) {
            clear();

            destroy_window_cache(*window_cache);
            *window_cache = nullptr;

            cz::heap_allocator().dealloc(cellss[0], *total_rows * *total_cols);
            cz::heap_allocator().dealloc(cellss[1], *total_rows * *total_cols);

            *total_rows = rows;
            *total_cols = cols;

            size_t grid_size = rows * cols;
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

    render_to_cells(cellss[1], window_cache, mini_buffer_window_cache, rows, cols, editor, client,
                    {});

    bool any = false;
    {
        ZoneScopedN("blit cells");
        int index = 0;
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                Cell* new_cell = &cellss[1][index];
                if (cellss[0][index] != *new_cell) {
                    int attrs = A_NORMAL;
                    if (new_cell->face.flags & Face::UNDERSCORE) {
                        attrs |= A_UNDERLINE;
                    }
                    if (new_cell->face.flags & Face::BOLD) {
                        attrs |= A_BOLD;
                    }
                    if (new_cell->face.flags & Face::REVERSE) {
                        attrs |= A_REVERSE;
                    }
                    if (new_cell->face.flags & Face::ITALICS) {
                        attrs |= A_UNDERLINE;
                    }
                    attrset(attrs);

                    int16_t bg = get_face_color_or(new_cell->face.background, 0);
                    if (colors[bg] == 0) {
                        bg = 0;
                    }
                    int16_t fg = get_face_color_or(new_cell->face.foreground, 7);
                    if (colors[fg] == 0) {
                        fg = 7;
                    }

                    int32_t color_pair = (colors[bg] - 1) * used_colors + (colors[fg] - 1) + 1;
                    color_set(color_pair, nullptr);

                    mvaddch(y, x, new_cell->code);
                    any = true;
                }
                ++index;
            }
        }
    }

    cz::swap(cellss[0], cellss[1]);

    {
        ZoneScopedN("refresh");
        if (any) {
            refresh();
        }
    }

    FrameMark;
}

static void process_key_press(Server* server, Client* client, int ch) {
    ZoneScoped;

    Key key = {};
rerun:
    if (ch == '\n' || ch == '\t') {
        key.code = ch;
    } else if (ch == 0) {
        // C-@, C-\\ .
        key.modifiers |= CONTROL;
        key.code = '@';
    } else if (ch >= 1 && ch <= 26) {
        // C-a, C-b, ..., C-z
        key.modifiers |= CONTROL;
        key.code = 'a' - 1 + ch;
    } else if (ch == 27) {
        // ESCAPE (\\^), C-[
        ch = getch();
        key.modifiers |= ALT;
        goto rerun;
    } else if (ch == 28) {
        key.modifiers |= CONTROL;
        key.code = '\\';
    } else if (ch == 29) {
        key.modifiers |= CONTROL;
        key.code = ']';
    } else if (ch == 30) {
        key.modifiers |= CONTROL;
        key.code = '^';
    } else if (ch == 31) {
        // C-_, C-/
        key.modifiers |= CONTROL;
        key.code = '_';
    } else if (ch == 127) {
        // BACKSPACE (\\-), C-?
        key.code = Key_Code::BACKSPACE;
    } else if (ch >= 0 && ch < 128) {
        // normal keys
        key.code = ch;
    } else if (ch == KEY_UP) {
        key.code = Key_Code::UP;
    } else if (ch == KEY_DOWN) {
        key.code = Key_Code::DOWN;
    } else if (ch == KEY_LEFT) {
        key.code = Key_Code::LEFT;
    } else if (ch == KEY_RIGHT) {
        key.code = Key_Code::RIGHT;
    } else {
        return;
    }

    server->receive(client, key);
}

static void process_key_presses(Server* server, Client* client, int ch) {
    ZoneScoped;

    while (1) {
        process_key_press(server, client, ch);
        if (client->queue_quit) {
            return;
        }

        ch = getch();
        if (ch == ERR) {
            break;
        }
    }
}

static int ncurses_copy(void*, cz::Str) {
    return 0;
}
static int ncurses_update_global_copy_chain(Copy_Chain**, void*) {
    return 0;
}

void run(Server* server, Client* client) {
    ZoneScoped;

    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);  // hide cursor
    CZ_DEFER(endwin());

    start_color();

    int16_t* colors = cz::heap_allocator().alloc_zeroed<int16_t>(COLORS);
    CZ_ASSERT(colors);
    CZ_DEFER(cz::heap_allocator().dealloc(colors, COLORS));
    colors[0] = 1;
    colors[7] = 1;
    colors[21] = 1;
    colors[208] = 1;
    colors[237] = 1;
    for (size_t i = 0; i < cz::len(server->editor.theme.special_faces); ++i) {
        Face* face = &server->editor.theme.special_faces[i];
        int16_t fg = get_face_color(face->foreground);
        if (fg >= 0 && fg < COLORS) {
            colors[fg] = 1;
        }
        int16_t bg = get_face_color(face->background);
        if (bg >= 0 && bg < COLORS) {
            colors[bg] = 1;
        }
    }
    for (size_t i = 0; i < cz::len(server->editor.theme.token_faces); ++i) {
        Face* face = &server->editor.theme.token_faces[i];
        int16_t fg = get_face_color(face->foreground);
        if (fg >= 0 && fg < COLORS) {
            colors[fg] = 1;
        }
        int16_t bg = get_face_color(face->background);
        if (bg >= 0 && bg < COLORS) {
            colors[bg] = 1;
        }
    }

    int16_t used_colors = 0;
    for (size_t i = 0; i < (size_t)COLORS; ++i) {
        if (colors[i] != 0) {
            colors[i] = ++used_colors;
        }
    }

    int32_t color_pair = 0;
    for (size_t bg = 0; bg < (size_t)COLORS; ++bg) {
        if (colors[bg] == 0) {
            continue;
        }

        for (size_t fg = 0; fg < (size_t)COLORS; ++fg) {
            if (colors[fg] == 0) {
                continue;
            }

            // int32_t color_pair = (colors[bg] - 1) * used_colors + (colors[fg] - 1) + 1;
            init_pair(++color_pair, fg, bg);
        }
    }

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

    nodelay(stdscr, TRUE);

    custom::client_created_callback(&server->editor, client);

    while (1) {
        ZoneScopedN("ncurses main loop");

        client->update_mini_buffer_completion_cache(&server->editor);
        load_mini_buffer_completion_cache(server, client);

        render(&total_rows, &total_cols, cellss, &window_cache, &mini_buffer_window_cache,
               &server->editor, client, colors, used_colors);

        bool has_jobs = false;
        has_jobs |= server->slurp_jobs();
        has_jobs |= server->run_synchronous_jobs(client);

        process_buffer_external_updates(&server->editor, client, client->window);

        int ch;
        if (has_jobs) {
            ch = getch();
            if (ch == ERR) {
                continue;
            }
        } else {
            timeout(10);
            ch = getch();
            nodelay(stdscr, TRUE);
        }

        process_key_presses(server, client, ch);

        if (client->queue_quit) {
            return;
        }
    }
}

}
}
}

#endif
