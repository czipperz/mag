#include "ncurses.hpp"

#include <ctype.h>
#include <ncurses.h>
#include "client.hpp"
#include "command_macros.hpp"
#include "server.hpp"
#include "token.hpp"

namespace mag {

struct Cell {
    int attrs;
    char code;

    bool operator==(const Cell& other) const { return attrs == other.attrs && code == other.code; }

    bool operator!=(const Cell& other) const { return !(*this == other); }
};

#define SET(ATTRS, CH)                                                       \
    do {                                                                     \
        Cell* cell = &cells[(y + start_row) * total_cols + (x + start_col)]; \
        cell->attrs = ATTRS;                                                 \
        cell->code = CH;                                                     \
    } while (0)

#define ADD_NEWLINE()                 \
    do {                              \
        for (; x < count_cols; ++x) { \
            SET(A_NORMAL, ' ');       \
        }                             \
        ++y;                          \
        x = 0;                        \
        if (y == count_rows) {        \
            return;                   \
        }                             \
    } while (0)

#define ADDCH(ATTRS, CH)       \
    do {                       \
        SET(ATTRS, CH);        \
        ++x;                   \
        if (x == count_cols) { \
            ADD_NEWLINE();     \
        }                      \
    } while (0)

static void draw_buffer_contents(Buffer* buffer,
                                 Cell* cells,
                                 int total_cols,
                                 Editor* editor,
                                 bool show_cursors,
                                 int start_row,
                                 int start_col,
                                 int count_rows,
                                 int count_cols) {
    int y = 0;
    int x = 0;

    Token token;
    bool has_token = buffer->mode.next_token(&buffer->contents, 0, &token);

    uint64_t contents_len = buffer->contents.len();
    int show_mark = 0;
    for (size_t i = 0; i < contents_len; ++i) {
        if (i == token.end) {
            has_token = buffer->mode.next_token(&buffer->contents, token.end, &token);
        }

        bool has_cursor = false;
        if (show_cursors) {
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
        }

#if 0
            if (buffer->contents.is_bucket_separator(i)) {
                ADDCH(A_NORMAL, '\'');
            }
#endif

        int attrs = A_NORMAL;
        if (has_cursor) {
            attrs |= A_REVERSE;
        }
        if (show_mark) {
            attrs |= A_REVERSE;
        }

        int type;
        if (has_token && i >= token.start && i < token.end) {
            type = token.type;
        } else {
            type = Token_Type::DEFAULT;
        }

        attrs |= COLOR_PAIR(type + 1);

        Face* face = &editor->theme.faces[type];
        if (face->flags & Face::BOLD) {
            attrs |= A_BOLD;
        }
        if (face->flags & Face::UNDERSCORE) {
            attrs |= A_UNDERLINE;
        }

        char ch = buffer->contents[i];
        if (ch == '\n') {
            ADDCH(attrs, ' ');
            ADD_NEWLINE();
        } else {
            ADDCH(attrs, ch);
        }
    }

    if (show_cursors) {
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            if (buffer->cursors[c].point == buffer->contents.len()) {
                SET(A_REVERSE, ' ');
                ++x;
                break;
            }
        }
    }

    for (; y < count_rows; ++y) {
        for (; x < count_cols; ++x) {
            SET(A_NORMAL, ' ');
        }
        x = 0;
    }
}

static void draw_buffer(Cell* cells,
                        int total_cols,
                        Editor* editor,
                        Buffer_Id buffer_id,
                        bool show_cursors,
                        int start_row,
                        int start_col,
                        int count_rows,
                        int count_cols) {
    WITH_BUFFER(buffer, buffer_id, {
        draw_buffer_contents(buffer, cells, total_cols, editor, show_cursors, start_row, start_col,
                             count_rows - 1, count_cols);

        int y = count_rows - 1;
        int x = 0;

        int attrs = A_REVERSE;
        SET(attrs, '-');
        ++x;
        SET(attrs, '-');
        ++x;
        SET(attrs, '-');
        ++x;
        SET(attrs, ' ');
        ++x;
        size_t max = cz::min<size_t>(buffer->name.len(), count_cols - x);
        size_t i;
        for (i = 0; i < max; ++i) {
            SET(attrs, buffer->name[i]);
            ++x;
        }
        for (; x < count_cols; ++x) {
            SET(attrs, ' ');
        }
    });
}

static void draw_window(Cell* cells,
                        int total_cols,
                        Editor* editor,
                        Window* window,
                        Window* selected_window,
                        int start_row,
                        int start_col,
                        int count_rows,
                        int count_cols) {
    switch (window->tag) {
    case Window::UNIFIED:
        draw_buffer(cells, total_cols, editor, window->v.unified_id, window == selected_window,
                    start_row, start_col, count_rows, count_cols);
        break;

    case Window::VERTICAL_SPLIT: {
        int left_cols = (count_cols - 1) / 2;
        int right_cols = count_cols - left_cols - 1;

        draw_window(cells, total_cols, editor, window->v.vertical_split.left, selected_window,
                    start_row, start_col, count_rows, left_cols);

        {
            int x = left_cols;
            for (int y = 0; y < count_rows; ++y) {
                SET(A_NORMAL, '|');
            }
        }

        draw_window(cells, total_cols, editor, window->v.vertical_split.right, selected_window,
                    start_row, start_col + count_cols - right_cols, count_rows, right_cols);
        break;
    }

    case Window::HORIZONTAL_SPLIT: {
        int top_rows = (count_rows - 1) / 2;
        int bottom_rows = count_rows - top_rows - 1;

        draw_window(cells, total_cols, editor, window->v.horizontal_split.top, selected_window,
                    start_row, start_col, top_rows, count_cols);

        {
            int y = top_rows;
            for (int x = 0; x < count_cols; ++x) {
                SET(A_NORMAL, '-');
            }
        }

        draw_window(cells, total_cols, editor, window->v.horizontal_split.bottom, selected_window,
                    start_row + count_rows - bottom_rows, start_col, bottom_rows, count_cols);
        break;
    }
    }
}

static void render_to_cells(Cell* cells,
                            int total_rows,
                            int total_cols,
                            Editor* editor,
                            Client* client) {
    draw_window(cells, total_cols, editor, client->window, client->_selected_window, 0, 0,
                total_rows - (client->_message.tag != Message::NONE), total_cols);

    if (client->_message.tag != Message::NONE) {
        int y = 0;
        int x = 0;
        int start_row = total_rows - 1;
        int start_col = 0;
        int attrs = A_NORMAL;

        for (size_t i = 0; i < client->_message.text.len; ++i) {
            SET(attrs, client->_message.text[i]);
            ++x;
        }

        if (client->_message.tag > Message::SHOW) {
            start_col = x;
            WITH_BUFFER(buffer, client->mini_buffer_id(), {
                draw_buffer_contents(buffer, cells, total_cols, editor, client->_select_mini_buffer,
                                     start_row, start_col, total_rows - start_row,
                                     total_cols - start_col);
            });
        } else {
            for (; x < total_cols; ++x) {
                SET(attrs, ' ');
            }

            if (std::chrono::system_clock::now() - client->_message_time >
                std::chrono::seconds(5)) {
                client->_message.tag = Message::NONE;
            }
        }
    }
}

static void render(int* total_rows,
                   int* total_cols,
                   Cell** cellss,
                   Editor* editor,
                   Client* client) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    if (rows != *total_rows || cols != *total_cols) {
        clear();

        free(cellss[0]);
        free(cellss[1]);

        *total_rows = rows;
        *total_cols = cols;

        size_t grid_size = rows * cols;
        cellss[0] = (Cell*)malloc(grid_size * sizeof(Cell));
        cellss[1] = (Cell*)malloc(grid_size * sizeof(Cell));

        for (size_t i = 0; i < grid_size; ++i) {
            cellss[0][i].attrs = 0;
            cellss[0][i].code = ' ';
            cellss[1][i].attrs = 0;
            cellss[1][i].code = ' ';
        }
    }

    render_to_cells(cellss[1], rows, cols, editor, client);

    int index = 0;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            Cell* new_cell = &cellss[1][index];
            if (cellss[0][index] != *new_cell) {
                attrset(new_cell->attrs);
                mvaddch(y, x, new_cell->code);
            }
            ++index;
        }
    }

    cz::swap(cellss[0], cellss[1]);

    refresh();
}

void run_ncurses(Server* server, Client* client) {
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);  // hide cursor

    start_color();
    for (size_t i = 0; i < server->editor.theme.faces.len(); ++i) {
        Face* face = &server->editor.theme.faces[i];
        init_pair(i + 1, face->foreground, face->background);
    }

    Cell* cellss[2] = {nullptr, nullptr};
    CZ_DEFER({
        free(cellss[0]);
        free(cellss[1]);
    });

    int total_rows = 0;
    int total_cols = 0;
    render(&total_rows, &total_cols, cellss, &server->editor, client);

    FILE* file = fopen("tmp.txt", "w");

    while (1) {
        int ch = getch();
        if (ch == ERR) {
            render(&total_rows, &total_cols, cellss, &server->editor, client);
            nodelay(stdscr, FALSE);
            continue;
        }

        nodelay(stdscr, TRUE);

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
    }

    fclose(file);

    endwin();
}

}
