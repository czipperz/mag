#include "ncurses.hpp"

#include <ctype.h>
#include <ncurses.h>
#include "client.hpp"
#include "command_macros.hpp"
#include "server.hpp"

namespace mag {

static void draw_buffer_in_box(Editor* editor,
                               Buffer_Id buffer_id,
                               int start_row,
                               int start_col,
                               int count_rows,
                               int count_cols) {
    int y = 0;
    int x = 0;

    WITH_BUFFER(buffer, buffer_id, {
        uint64_t contents_len = buffer->contents.len();
        int show_mark = 0;
        for (size_t i = 0; i < contents_len; ++i) {
            bool has_cursor = false;
            for (size_t c = 0; c < buffer->cursors.len(); ++c) {
                Cursor* cursor = &buffer->cursors[c];
                if (buffer->show_marks) {
                    if (i == std::min(cursor->mark, cursor->point)) {
                        ++show_mark;
                    }
                    if (i == std::max(cursor->mark, cursor->point)) {
                        --show_mark;
                    }
                }
                if (i == cursor->point) {
                    has_cursor = true;
                }
            }

#if 0
            if (buffer->contents.is_bucket_separator(i)) {
                attrset(A_NORMAL);
                addch('\'');
            }
#endif

            int attrs = A_NORMAL;
            if (has_cursor) {
                attrs |= A_REVERSE;
            }
            if (show_mark) {
                attrs |= A_REVERSE;
            }
            attrset(attrs);

#define ADD_NEWLINE()              \
    do {                           \
        ++y;                       \
        x = 0;                     \
        if (y == count_rows - 1) { \
            goto draw_bottom_row;  \
        }                          \
    } while (0)

#define ADDCH(CH)                                  \
    do {                                           \
        mvaddch(y + start_row, x + start_col, CH); \
        ++x;                                       \
        if (x == count_cols) {                     \
            ADD_NEWLINE();                         \
        }                                          \
    } while (0)

            char ch = buffer->contents[i];
            if (ch == '\n') {
                ADDCH(' ');
                ADD_NEWLINE();
            } else {
                ADDCH(ch);
            }
        }

        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            if (buffer->cursors[c].point == buffer->contents.len()) {
                attrset(A_REVERSE);
                mvaddch(y + start_row, x + start_col, ' ');
                break;
            }
        }

    draw_bottom_row:
        move(start_row + count_rows - 1, start_col);

        attrset(A_REVERSE);
        addch('-');
        addch('-');
        addch('-');
        addch(' ');
        size_t max = cz::min<size_t>(buffer->name.len(), count_cols - 4);
        size_t i;
        for (i = 0; i < max; ++i) {
            addch(buffer->name[i]);
        }
        for (; i < (size_t)count_cols - 4; ++i) {
            addch(' ');
        }
    });
}

static void draw_window(Editor* editor,
                        Window* window,
                        int start_row,
                        int start_col,
                        int count_rows,
                        int count_cols) {
    switch (window->tag) {
    case Window::UNIFIED:
        draw_buffer_in_box(editor, window->v.unified_id, start_row, start_col, count_rows,
                           count_cols);
        break;

    case Window::VERTICAL_SPLIT: {
        int left_cols = (count_cols - 1) / 2;
        int right_cols = count_cols - left_cols - 1;

        draw_window(editor, window->v.vertical_split.left,
                    start_row, start_col,
                    count_rows, left_cols);

        attrset(A_NORMAL);
        for (int row = 0; row < count_rows; ++row) {
            mvaddch(row, left_cols, '|');
        }

        draw_window(editor, window->v.vertical_split.right,
                    start_row, start_col + count_cols - right_cols,
                    count_rows, right_cols);
        break;
    }

    case Window::HORIZONTAL_SPLIT: {
        int top_rows = (count_rows - 1) / 2;
        int bottom_rows = count_rows - top_rows - 1;

        draw_window(editor, window->v.horizontal_split.top,
                    start_row, start_col,
                    top_rows, count_cols);

        attrset(A_NORMAL);
        for (int col = 0; col < count_cols; ++col) {
            mvaddch(top_rows, col, '-');
        }

        draw_window(editor, window->v.horizontal_split.bottom,
                    start_row + count_rows - bottom_rows, start_col,
                    bottom_rows, count_cols);
        break;
    }
    }
}

static void render(Editor* editor, Client* client) {
    clear();
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    move(0, 0);

    draw_window(editor, client->window, 0, 0, rows - (client->_message.tag != Message::NONE), cols);

    {
        move(rows - 1, 0);
        attrset(A_NORMAL);
        if (client->_message.tag != Message::NONE) {
            for (size_t i = 0; i < client->_message.text.len; ++i) {
                addch(client->_message.text[i]);
            }

            if (std::chrono::system_clock::now() - client->_message_time >
                std::chrono::seconds(5)) {
                client->_message.tag = Message::NONE;
            }
        }
    }

    refresh();
}

void run_ncurses(Server* server, Client* client) {
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);  // hide cursor

    render(&server->editor, client);

    FILE* file = fopen("tmp.txt", "w");

    while (1) {
        int ch = getch();
        if (ch == ERR) {
            continue;
        }

        Key key = {};
    rerun:
        fprintf(file, "%s %d\n", keyname(ch), ch);
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
            continue;
        }

        server->receive(client, key);
        if (client->queue_quit) {
            break;
        }
        render(&server->editor, client);
    }

    fclose(file);

    endwin();
}
}
