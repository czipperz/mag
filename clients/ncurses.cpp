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
    int16_t max_colors = 256;
    if (color < 0 || color >= max_colors) {
        color = deflt;
    }
    return color;
}

namespace {
struct NcursesColorPair {
    int16_t fg;
    int16_t bg;
};
}

/// NCurses supports colors via preassigning color pairs.  But we don't know all the colors
/// the user will use until they use them.  For example `buffer_created_callback` can create
/// arbitrary overlays.  Thus we dynamically allocate any additional colors that come in.
static size_t get_color_pair_or_assign(NcursesColorPair* color_pairs,
                                       size_t* num_allocated_colors,
                                       const Face& face,
                                       int* attrs) {
    int16_t fg = get_face_color_or(face.foreground, 7);
    int16_t bg = get_face_color_or(face.background, 0);
    for (size_t i = 0; i < (size_t)COLOR_PAIRS; ++i) {
        if (color_pairs[i].fg == fg && color_pairs[i].bg == bg) {
            return i;
        }
    }
    for (size_t i = 0; i < (size_t)COLOR_PAIRS; ++i) {
        if (color_pairs[i].bg == fg && color_pairs[i].fg == bg) {
            *attrs ^= A_REVERSE;
            return i;
        }
    }

    // Too many colors.  Fail!
    if (*num_allocated_colors == (size_t)COLOR_PAIRS) {
        return 0;
    }

    size_t i = *num_allocated_colors;
    init_pair(i, fg, bg);
    color_pairs[i] = {fg, bg};
    ++*num_allocated_colors;
    return i;
}

static void render(int* total_rows,
                   int* total_cols,
                   Cell** cellss,
                   Window_Cache** window_cache,
                   Window_Cache** mini_buffer_window_cache,
                   Editor* editor,
                   Client* client,
                   NcursesColorPair* color_pairs,
                   size_t* num_allocated_colors) {
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

                    size_t color_pair = get_color_pair_or_assign(color_pairs, num_allocated_colors,
                                                                 new_cell->face, &attrs);

                    attrset(attrs);
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

static Key key_map[1024];
static void bind_key(int ch, uint16_t modifiers, uint16_t code) {
    CZ_ASSERT(ch >= 0);
    CZ_ASSERT(ch < CZ_DIM(key_map));
    key_map[ch] = {modifiers, code};
}

static void bind_arrow_keys() {
    bind_key(KEY_UP, 0, Key_Code::UP);
    bind_key(KEY_DOWN, 0, Key_Code::DOWN);
    bind_key(KEY_LEFT, 0, Key_Code::LEFT);
    bind_key(KEY_RIGHT, 0, Key_Code::RIGHT);

    bind_key(565, ALT, Key_Code::UP);
    bind_key(524, ALT, Key_Code::DOWN);
    bind_key(544, ALT, Key_Code::LEFT);
    bind_key(559, ALT, Key_Code::RIGHT);

    bind_key(KEY_SR, SHIFT, Key_Code::UP);
    bind_key(KEY_SF, SHIFT, Key_Code::DOWN);
    bind_key(KEY_SLEFT, SHIFT, Key_Code::LEFT);
    bind_key(KEY_SRIGHT, SHIFT, Key_Code::RIGHT);

    bind_key(567, CONTROL, Key_Code::UP);
    bind_key(526, CONTROL, Key_Code::DOWN);
    bind_key(546, CONTROL, Key_Code::LEFT);
    bind_key(561, CONTROL, Key_Code::RIGHT);

    bind_key(569, (CONTROL | ALT), Key_Code::UP);
    bind_key(528, (CONTROL | ALT), Key_Code::DOWN);
    bind_key(548, (CONTROL | ALT), Key_Code::LEFT);
    bind_key(563, (CONTROL | ALT), Key_Code::RIGHT);

    bind_key(547, (CONTROL | SHIFT), Key_Code::LEFT);
    bind_key(562, (CONTROL | SHIFT), Key_Code::RIGHT);
}

static void bind_function_keys() {
    for (int n = 0; n < 12; ++n) {
        Key_Code code = (Key_Code)(Key_Code::F1 + n);
        bind_key(265 + n + 0 * 12, 0, code);
        bind_key(265 + n + 1 * 12, SHIFT, code);
        bind_key(265 + n + 2 * 12, CONTROL, code);
        bind_key(265 + n + 3 * 12, (CONTROL | SHIFT), code);
        bind_key(265 + n + 4 * 12, ALT, code);
        bind_key(265 + n + 5 * 12, (ALT | SHIFT), code);
    }
}

static void bind_side_special_key(int ch_normal, int ch_shift, int ch_base, Key_Code code) {
    bind_key(ch_normal, 0, code);
    bind_key(ch_shift, SHIFT, code);
    bind_key(ch_base + 0, ALT, code);
    bind_key(ch_base + 1, (ALT | SHIFT), code);
    bind_key(ch_base + 2, CONTROL, code);
    bind_key(ch_base + 3, (CONTROL | SHIFT), code);
    bind_key(ch_base + 4, (CONTROL | ALT), code);
}

static void bind_side_special_keys() {
    bind_side_special_key(KEY_HOME, KEY_SHOME, 534, Key_Code::HOME);
    bind_side_special_key(KEY_END, KEY_SEND, 529, Key_Code::END);
    bind_side_special_key(KEY_NPAGE, KEY_SNEXT, 549, Key_Code::PAGE_DOWN);
    bind_side_special_key(KEY_PPAGE, KEY_SPREVIOUS, 554, Key_Code::PAGE_UP);
    bind_side_special_key(KEY_DC, KEY_SDC, 518, Key_Code::DELETE_);
    bind_side_special_key(KEY_IC, KEY_SIC, 539, Key_Code::INSERT);
}

static void bind_all_keys() {
    // C-a, C-b, ..., C-z
    for (int ch = 1; ch <= 26; ++ch) {
        bind_key(ch, CONTROL, 'a' - 1 + ch);
    }

    // Default for ASCII keys is to bind to themselves.
    for (int ch = 27; ch < 128; ++ch) {
        bind_key(ch, 0, ch);
    }

    bind_key(KEY_BTAB, SHIFT, '\t');
    bind_key(0, CONTROL, '@');  // C-@, C-\\ .

    bind_key('\n', 0, '\n');
    bind_key('\t', 0, '\t');
    bind_key(28, CONTROL, '\\');
    bind_key(29, CONTROL, ']');
    bind_key(30, CONTROL, '^');
    bind_key(31, CONTROL, '_');  // C-_, C-/
    bind_key(KEY_BACKSPACE, 0, Key_Code::BACKSPACE);
    bind_key(127, 0, Key_Code::BACKSPACE); /*(\\-), C-?*/

    bind_arrow_keys();
    bind_function_keys();
    bind_side_special_keys();
}

static void print_unbound_key_message(Client* client, int ch) {
    cz::String octal = cz::asprintf("0%o", ch);
    CZ_DEFER(octal.drop(cz::heap_allocator()));
    cz::String message = cz::format("Ignoring unknown key code: ", ch, " = ", octal);
    CZ_DEFER(message.drop(cz::heap_allocator()));
    client->show_message(message);
}

static void process_key_press(Server* server, Client* client, int ch) {
    ZoneScoped;

    uint16_t modifiers = 0;
rerun:
    if (ch == ERR) {
        // No actual key, just return.
        return;
    } else if (ch == 27) {
        // ESCAPE (\\^), C-[
        ch = getch();
        modifiers |= ALT;
        goto rerun;
    } else if (ch == KEY_MOUSE) {
        process_mouse_event(server, client);
        return;
    } else if (ch == KEY_RESIZE) {
        // We poll window size instead of handling resize events.
        return;
    } else if (ch >= CZ_DIM(key_map)) {
        print_unbound_key_message(client, ch);
        return;
    }

    Key key = key_map[ch];
    if (key.modifiers == 0 && key.code == 0) {
        print_unbound_key_message(client, ch);
        return;
    }
    key.modifiers |= modifiers;

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

void run(Server* server, Client* client) {
    ZoneScoped;

    newterm(nullptr, fopen("/dev/tty", "wb"), fopen("/dev/tty", "rb"));
    def_prog_mode();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);  // hide cursor
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    mouseinterval(0);
    CZ_DEFER(endwin());

    start_color();
    bind_all_keys();

    NcursesColorPair* color_pairs = cz::heap_allocator().alloc<NcursesColorPair>(COLOR_PAIRS);
    CZ_ASSERT(color_pairs);
    size_t num_allocated_colors = 1;
    color_pairs[0] = {7, 0};  // Color pair 0 is hardcoded to default foregrand & background.
    for (size_t i = 1; i < (size_t)COLOR_PAIRS; ++i) {
        color_pairs[i] = {-1, -1};
    }
    CZ_DEFER(cz::heap_allocator().dealloc(color_pairs, COLOR_PAIRS));

    int attrs = 0;
    for (const Face& face : server->editor.theme.special_faces) {
        get_color_pair_or_assign(color_pairs, &num_allocated_colors, face, &attrs);
    }
    for (const Face& face : server->editor.theme.token_faces) {
        get_color_pair_or_assign(color_pairs, &num_allocated_colors, face, &attrs);
    }

    if (custom::enable_terminal_colors && can_change_color() && COLORS >= 256) {
        for (size_t i = 0; i < 256; ++i) {
            uint32_t c = server->editor.theme.colors[i];
            uint8_t r = (c & 0xFF0000) >> 16;
            uint8_t g = (c & 0x00FF00) >> 8;
            uint8_t b = (c & 0x0000FF);
            init_color(i, (short)((double)r * 1000.0 / 255.0), (short)((double)g * 1000.0 / 255.0),
                       (short)((double)b * 1000.0 / 255.0));
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

        client->update_mini_buffer_completion_cache();
        load_mini_buffer_completion_cache(server, client);

        render(&total_rows, &total_cols, cellss, &window_cache, &mini_buffer_window_cache,
               &server->editor, client, color_pairs, &num_allocated_colors);

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
