#include "ncurses.hpp"

#include <ctype.h>
#include <ncurses.h>
#include <Tracy.hpp>
#include <algorithm>
#include <cz/bit_array.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <thread>
#include "cell.hpp"
#include "client.hpp"
#include "completion.hpp"
#include "movement.hpp"
#include "render.hpp"
#include "server.hpp"
#include "token.hpp"
#include "window_cache.hpp"

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

static bool getch_callback(void* data) {
    int* out = (int*)data;
    *out = getch();
    return *out != ERR;
}

static void render(int* total_rows,
                   int* total_cols,
                   Cell** cellss,
                   Window_Cache** window_cache,
                   Editor* editor,
                   Client* client,
                   int16_t* colors,
                   int16_t used_colors) {
    ZoneScoped;
    FrameMarkStart("ncurses");

    int rows, cols;
    {
        ZoneScopedN("detect resize");
        getmaxyx(stdscr, rows, cols);

        if (rows != *total_rows || cols != *total_cols) {
            clear();

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

                    int16_t bg = new_cell->face.background;
                    if (bg < 0 || bg >= COLORS) {
                        bg = 0;
                    }
                    int16_t fg = new_cell->face.foreground;
                    if (fg < 0 || fg >= COLORS) {
                        fg = 7;
                    }
                    int32_t color_pair = (colors[bg] - 1) * used_colors + (colors[fg] - 1) + 1;
                    attrs |= COLOR_PAIR(color_pair);

                    attrset(attrs);
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

    FrameMarkEnd("ncurses");
}

static void process_key_press(Server* server, Client* client, char ch) {
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
        key.code = 127;
    } else if (ch >= 0 && ch < 128) {
        // normal keys
        key.code = ch;
    } else {
        return;
    }

    server->receive(client, key);
}

static void process_key_presses(Server* server, Client* client, char ch) {
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

void run(Server* server, Client* client) {
    ZoneScoped;

    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);  // hide cursor
    CZ_DEFER(endwin());

    start_color();

    int16_t colors[COLORS] = {};
    colors[0] = 1;
    colors[7] = 1;
    colors[21] = 1;
    colors[237] = 1;
    for (size_t i = 0; i < server->editor.theme.faces.len(); ++i) {
        Face* face = &server->editor.theme.faces[i];
        int16_t fg = face->foreground;
        if (fg >= 0 && fg < COLORS) {
            colors[fg] = 1;
        }
        int16_t bg = face->background;
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

    Cell* cellss[2] = {nullptr, nullptr};
    CZ_DEFER({
        free(cellss[0]);
        free(cellss[1]);
    });

    Window_Cache* window_cache = nullptr;
    CZ_DEFER(destroy_window_cache(window_cache));

    int total_rows = 0;
    int total_cols = 0;

    nodelay(stdscr, TRUE);

    while (1) {
        ZoneScopedN("ncurses main loop");

        render(&total_rows, &total_cols, cellss, &window_cache, &server->editor, client, colors,
               used_colors);

        int ch = ERR;
        if (client->mini_buffer_completion_cache.results.state != Completion_Results::LOADED &&
            client->_message.completion_engine) {
            client->_message.completion_engine(&client->mini_buffer_completion_cache.results);
            continue;
        }

        bool has_jobs = server->editor.jobs.len() > 0;
        server->editor.tick_jobs();

        if (ch == ERR) {
            cache_windows_check_points(window_cache, client->window, &server->editor,
                                       getch_callback, &ch);
        }

        if (ch == ERR) {
            if (has_jobs) {
                ch = getch();
                if (ch == ERR) {
                    continue;
                }
            } else {
                nodelay(stdscr, FALSE);
                ch = getch();
                nodelay(stdscr, TRUE);
            }
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
