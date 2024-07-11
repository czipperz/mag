#ifdef HAS_NCURSES

#include "ncurses.hpp"

#include <ncurses.h>
#include <algorithm>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/format.hpp>
#include <cz/heap.hpp>
#include <thread>
#include <tracy/Tracy.hpp>
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

#undef MOCK_INPUT       // read from premade input buffer instead of keyboard
#undef LOG_GETCH        // log all characters typed
#undef VISUALIZE_INPUT  // show messages for characters typed instead of processing them

#ifdef MOCK_INPUT
static bool is_nodelay = false;
#define nodelay(WINDOW, VALUE) (is_nodelay = (VALUE))
#endif

static int logging_getch() {
    int val = getch();

#ifdef LOG_GETCH
    if (val != ERR) {
        static cz::Output_File file;
        if (!file.is_open())
            CZ_ASSERT(file.open("/tmp/mag-key-log"));

        cz::Heap_String str = cz::format(val);
        if (cz::is_print(val))
            cz::append(&str, " '", (char)val, "'\n");
        else
            cz::append(&str, '\n');
        file.write(str);
        str.drop();
    }
#endif

    return val;
}

static int mock_getch() {
    ZoneScoped;

#ifndef MOCK_INPUT
    return logging_getch();
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

    bool any_animated_scrolling = false;
    render_to_cells(cellss[1], window_cache, mini_buffer_window_cache, rows, cols, editor, client,
                    &any_animated_scrolling);

    bool any = any_animated_scrolling;
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

static void process_mouse_event(Server* server, Client* client) {
    MEVENT mevent;
    if (getmouse(&mevent) != OK) {
        return;
    }

    client->mouse.has_client_position = true;
    client->mouse.client_row = mevent.y;
    client->mouse.client_column = mevent.x;
    recalculate_mouse(server->editor.theme, client);

    Key key = {};
    if (mevent.bstate & BUTTON_CTRL) {
        key.modifiers |= CONTROL;
    }
    if (mevent.bstate & BUTTON_ALT) {
        key.modifiers |= ALT;
    }
    if (mevent.bstate & BUTTON_SHIFT) {
        key.modifiers |= SHIFT;
    }

    if (mevent.bstate & BUTTON1_PRESSED) {
        key.code = Key_Code::MOUSE1;
        server->receive(client, key);
    }
    if (mevent.bstate & BUTTON1_RELEASED) {
        key.code = Key_Code::MOUSE1;
        server->release(client, key);
    }
    if (mevent.bstate & BUTTON2_PRESSED) {
        key.code = Key_Code::MOUSE2;
        server->receive(client, key);
    }
    if (mevent.bstate & BUTTON2_RELEASED) {
        key.code = Key_Code::MOUSE2;
        server->release(client, key);
    }
    if (mevent.bstate & BUTTON3_PRESSED) {
        key.code = Key_Code::MOUSE3;
        server->receive(client, key);
    }
    if (mevent.bstate & BUTTON3_RELEASED) {
        key.code = Key_Code::MOUSE3;
        server->release(client, key);
    }
    if (mevent.bstate & BUTTON4_PRESSED) {
        key.code = Key_Code::MOUSE4;
        server->receive(client, key);
    }
    if (mevent.bstate & BUTTON4_RELEASED) {
        key.code = Key_Code::MOUSE4;
        server->release(client, key);
    }
#ifdef BUTTON5_PRESSED
    if (mevent.bstate & BUTTON5_PRESSED) {
        key.code = Key_Code::MOUSE5;
        server->receive(client, key);
    }
    if (mevent.bstate & BUTTON5_RELEASED) {
        key.code = Key_Code::MOUSE5;
        server->release(client, key);
    }
#endif
}

static bool handle_arrow_keys(int ch, Key& key) {
    if (ch == KEY_UP) {
        key.code = Key_Code::UP;
    } else if (ch == KEY_DOWN) {
        key.code = Key_Code::DOWN;
    } else if (ch == KEY_LEFT) {
        key.code = Key_Code::LEFT;
    } else if (ch == KEY_RIGHT) {
        key.code = Key_Code::RIGHT;

    } else if (ch == 565) {
        key.code = Key_Code::UP;
        key.modifiers |= ALT;
    } else if (ch == 524) {
        key.code = Key_Code::DOWN;
        key.modifiers |= ALT;
    } else if (ch == 544) {
        key.code = Key_Code::LEFT;
        key.modifiers |= ALT;
    } else if (ch == 559) {
        key.code = Key_Code::RIGHT;
        key.modifiers |= ALT;

    } else if (ch == KEY_SR) {
        key.code = Key_Code::UP;
        key.modifiers |= SHIFT;
    } else if (ch == KEY_SF) {
        key.code = Key_Code::DOWN;
        key.modifiers |= SHIFT;
    } else if (ch == KEY_SLEFT) {
        key.code = Key_Code::LEFT;
        key.modifiers |= SHIFT;
    } else if (ch == KEY_SRIGHT) {
        key.code = Key_Code::RIGHT;
        key.modifiers |= SHIFT;

    } else if (ch == 567) {
        key.code = Key_Code::UP;
        key.modifiers |= CONTROL;
    } else if (ch == 526) {
        key.code = Key_Code::DOWN;
        key.modifiers |= CONTROL;
    } else if (ch == 546) {
        key.code = Key_Code::LEFT;
        key.modifiers |= CONTROL;
    } else if (ch == 561) {
        key.code = Key_Code::RIGHT;
        key.modifiers |= CONTROL;

    } else if (ch == 569) {
        key.code = Key_Code::UP;
        key.modifiers |= (CONTROL | ALT);
    } else if (ch == 528) {
        key.code = Key_Code::DOWN;
        key.modifiers |= (CONTROL | ALT);
    } else if (ch == 548) {
        key.code = Key_Code::LEFT;
        key.modifiers |= (CONTROL | ALT);
    } else if (ch == 563) {
        key.code = Key_Code::RIGHT;
        key.modifiers |= (CONTROL | ALT);

    } else if (ch == 547) {
        key.code = Key_Code::LEFT;
        key.modifiers |= (CONTROL | SHIFT);
    } else if (ch == 562) {
        key.code = Key_Code::RIGHT;
        key.modifiers |= (CONTROL | SHIFT);

    } else {
        return false;
    }
    return true;
}

static bool handle_function_keys(int ch, Key& key) {
    if (ch >= 265 && ch <= 315) {
        // Main block of function keys (F1-F12).
        if (ch < 277) {
            key.code = Key_Code::F1 + (ch - 265);
        } else if (ch < 289) {
            key.code = Key_Code::F1 + (ch - 277);
            key.modifiers |= SHIFT;
        } else if (ch < 301) {
            key.code = Key_Code::F1 + (ch - 289);
            key.modifiers |= CONTROL;
        } else if (ch < 313) {
            key.code = Key_Code::F1 + (ch - 301);
            key.modifiers |= (CONTROL | SHIFT);
        } else {
            // Only A-F1 to A-F3 are bound.
            key.code = Key_Code::F1 + (ch - 313);
            key.modifiers |= ALT;
        }
    } else if (ch >= 325 && ch <= 327) {
        // Only A-S-F1 to A-S-F3 are bound.
        key.code = Key_Code::F1 + (ch - 313);
        key.modifiers |= (ALT | SHIFT);
    } else {
        return false;
    }
    return true;
}

static bool handle_side_special_keys(int ch, Key& key) {
    if (ch == KEY_HOME) {
        key.code = Key_Code::HOME;
    } else if (ch == KEY_SHOME) {
        key.code = Key_Code::HOME;
        key.modifiers |= SHIFT;
    } else if (ch == 534) {
        key.code = Key_Code::HOME;
        key.modifiers |= ALT;
    } else if (ch == 535) {
        key.code = Key_Code::HOME;
        key.modifiers |= (ALT | SHIFT);
    } else if (ch == 536) {
        key.code = Key_Code::HOME;
        key.modifiers |= CONTROL;
    } else if (ch == 538) {
        key.code = Key_Code::HOME;
        key.modifiers |= (CONTROL | ALT);

    } else if (ch == KEY_END) {
        key.code = Key_Code::END;
    } else if (ch == KEY_SEND) {
        key.code = Key_Code::END;
        key.modifiers |= SHIFT;
    } else if (ch == 529) {
        key.code = Key_Code::END;
        key.modifiers |= ALT;
    } else if (ch == 530) {
        key.code = Key_Code::END;
        key.modifiers |= (ALT | SHIFT);
    } else if (ch == 531) {
        key.code = Key_Code::END;
        key.modifiers |= CONTROL;
    } else if (ch == 533) {
        key.code = Key_Code::END;
        key.modifiers |= (CONTROL | ALT);

    } else if (ch == KEY_NPAGE) {
        key.code = Key_Code::PAGE_DOWN;
    } else if (ch == KEY_SNEXT) {
        key.code = Key_Code::PAGE_DOWN;
        key.modifiers |= SHIFT;
    } else if (ch == 549) {
        key.code = Key_Code::PAGE_DOWN;
        key.modifiers |= ALT;
    } else if (ch == 550) {
        key.code = Key_Code::PAGE_DOWN;
        key.modifiers |= (ALT | SHIFT);
    } else if (ch == 551) {
        key.code = Key_Code::PAGE_DOWN;
        key.modifiers |= CONTROL;
    } else if (ch == 553) {
        key.code = Key_Code::PAGE_DOWN;
        key.modifiers |= (CONTROL | ALT);

    } else if (ch == KEY_PPAGE) {
        key.code = Key_Code::PAGE_UP;
    } else if (ch == KEY_SPREVIOUS) {
        key.code = Key_Code::PAGE_UP;
        key.modifiers |= SHIFT;
    } else if (ch == 554) {
        key.code = Key_Code::PAGE_UP;
        key.modifiers |= ALT;
    } else if (ch == 555) {
        key.code = Key_Code::PAGE_UP;
        key.modifiers |= (ALT | SHIFT);
    } else if (ch == 556) {
        key.code = Key_Code::PAGE_UP;
        key.modifiers |= CONTROL;
    } else if (ch == 558) {
        key.code = Key_Code::PAGE_UP;
        key.modifiers |= (CONTROL | ALT);

    } else if (ch == KEY_DC) {
        key.code = Key_Code::DELETE_;
    } else if (ch == KEY_SDC) {
        key.code = Key_Code::DELETE_;
        key.modifiers |= SHIFT;
    } else if (ch == 518) {
        key.code = Key_Code::DELETE_;
        key.modifiers |= ALT;
    } else if (ch == 519) {
        key.code = Key_Code::DELETE_;
        key.modifiers |= ALT | SHIFT;
    } else if (ch == 520) {
        key.code = Key_Code::DELETE_;
        key.modifiers |= CONTROL;
    } else if (ch == 521) {
        key.code = Key_Code::DELETE_;
        key.modifiers |= (CONTROL | SHIFT);

    } else if (ch == KEY_IC) {
        key.code = Key_Code::INSERT;
    } else if (ch == KEY_SIC) {
        key.code = Key_Code::INSERT;
        key.modifiers |= SHIFT;
    } else if (ch == 539) {
        key.code = Key_Code::INSERT;
        key.modifiers |= ALT;
    } else if (ch == 540) {
        key.code = Key_Code::INSERT;
        key.modifiers |= ALT | SHIFT;
    } else if (ch == 541) {
        key.code = Key_Code::INSERT;
        key.modifiers |= CONTROL;
    } else if (ch == 542) {
        key.code = Key_Code::INSERT;
        key.modifiers |= (CONTROL | SHIFT);
    } else if (ch == 543) {
        key.code = Key_Code::INSERT;
        key.modifiers |= (CONTROL | ALT);

    } else {
        return false;
    }
    return true;
}

static void process_key_press(Server* server, Client* client, int ch) {
    ZoneScoped;

    Key key = {};
rerun:
    if (ch == ERR) {
        // No actual key, just return.
        return;
    } else if (ch == '\n' || ch == '\t') {
        key.code = ch;
    } else if (ch == KEY_BTAB) {
        key.code = '\t';
        key.modifiers |= SHIFT;
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
    } else if (ch == 127 /*(\\-), C-?*/ || ch == KEY_BACKSPACE) {
        key.code = Key_Code::BACKSPACE;
    } else if (ch >= 0 && ch < 128) {
        // normal keys
        key.code = ch;
    } else if (handle_arrow_keys(ch, key)) {
    } else if (handle_function_keys(ch, key)) {
    } else if (handle_side_special_keys(ch, key)) {
    } else if (ch == KEY_MOUSE) {
        process_mouse_event(server, client);
        return;
    } else if (ch == KEY_RESIZE) {
        // We poll window size instead of handling resize events.
        return;
    } else {
        cz::String octal = cz::asprintf("0%o", ch);
        CZ_DEFER(octal.drop(cz::heap_allocator()));
        cz::String message = cz::format("Ignoring unknown key code: ", ch, " = ", octal);
        CZ_DEFER(message.drop(cz::heap_allocator()));
        client->show_message(message);
        return;
    }

#ifndef VISUALIZE_INPUT
    server->receive(client, key);
#else
    if (key.modifiers == ALT && (key.code == 'x' || key.code == 'c')) {
        server->receive(client, key);
    } else {
        cz::String octal = cz::asprintf("0%o", ch);
        CZ_DEFER(octal.drop(cz::heap_allocator()));
        cz::String msg = {};
        CZ_DEFER(msg.drop(cz::heap_allocator()));
        cz::append(cz::heap_allocator(), &msg, "Pressed ", ch, " = ", octal, ": ");
        msg.reserve(cz::heap_allocator(), stringify_key_max_size);
        stringify_key(&msg, key);
        client->show_message(msg);
    }
#endif
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

static void mark_used_colors(int16_t* colors, Server* server) {
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
}

static int16_t assign_color_codes(int16_t* colors) {
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

    return used_colors;
}

void run(Server* server, Client* client) {
    ZoneScoped;

    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);  // hide cursor
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    mouseinterval(0);
    CZ_DEFER(endwin());

    start_color();

    int16_t* colors = cz::heap_allocator().alloc_zeroed<int16_t>(COLORS);
    CZ_ASSERT(colors);
    CZ_DEFER(cz::heap_allocator().dealloc(colors, COLORS));

    mark_used_colors(colors, server);
    int16_t used_colors = assign_color_codes(colors);

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

        client->update_mini_buffer_completion_cache();
        load_mini_buffer_completion_cache(server, client);

        render(&total_rows, &total_cols, cellss, &window_cache, &mini_buffer_window_cache,
               &server->editor, client, colors, used_colors);

        bool has_jobs = false;
        has_jobs |= server->slurp_jobs();
        has_jobs |= server->run_synchronous_jobs(client);

        process_buffer_external_updates(client, client->window);

        int ch;
        if (has_jobs) {
            ch = getch();
            if (ch == ERR) {
                continue;
            }
        } else {
            server->set_async_locked(false);
            timeout(10);
            ch = getch();
            nodelay(stdscr, TRUE);
            server->set_async_locked(true);
        }

        process_key_presses(server, client, ch);
        server->process_key_chain(client);

        if (client->queue_quit) {
            return;
        }
    }
}

}
}
}

#endif
