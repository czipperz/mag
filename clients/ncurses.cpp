#include "ncurses.hpp"

#include <ctype.h>
#include <ncurses.h>
#include <cz/bit_array.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "movement.hpp"
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

struct Tokenizer_Check_Point {
    uint64_t position;
    uint64_t state;
};

struct Window_Cache {
    Window::Tag tag;
    union {
        struct {
            Buffer_Id id;
            size_t change_index;
            uint64_t visible_end;
            cz::Vector<Tokenizer_Check_Point> tokenizer_check_points;
        } unified;
        struct {
            Window_Cache* left;
            Window_Cache* right;
        } vertical_split;
        struct {
            Window_Cache* top;
            Window_Cache* bottom;
        } horizontal_split;
    } v;
};

static void destroy_window_cache(Window_Cache* window_cache);
static void destroy_window_cache_children(Window_Cache* window_cache) {
    switch (window_cache->tag) {
    case Window::UNIFIED:
        window_cache->v.unified.tokenizer_check_points.drop(cz::heap_allocator());
        break;
    case Window::VERTICAL_SPLIT:
        destroy_window_cache(window_cache->v.vertical_split.left);
        destroy_window_cache(window_cache->v.vertical_split.right);
        break;
    case Window::HORIZONTAL_SPLIT:
        destroy_window_cache(window_cache->v.horizontal_split.top);
        destroy_window_cache(window_cache->v.horizontal_split.bottom);
        break;
    }
}

static void destroy_window_cache(Window_Cache* window_cache) {
    if (!window_cache) {
        return;
    }

    destroy_window_cache_children(window_cache);

    free(window_cache);
}

static void compute_visible_end(Buffer* buffer,
                                Contents_Iterator* line_start_iterator,
                                int count_rows,
                                int count_cols) {
    int rows;
    for (rows = 0; rows < count_rows - 1;) {
        Contents_Iterator next_line_start_iterator = *line_start_iterator;
        forward_line(buffer, &next_line_start_iterator);
        if (next_line_start_iterator.position == line_start_iterator->position) {
            break;
        }

        int line_rows =
            (next_line_start_iterator.position - line_start_iterator->position + count_cols - 1) /
            count_cols;
        *line_start_iterator = next_line_start_iterator;

        rows += line_rows;
    }
}

static void compute_visible_start(Buffer* buffer,
                                  Contents_Iterator* line_start_iterator,
                                  int count_rows,
                                  int count_cols) {
    for (int rows = 0; rows < count_rows - 2;) {
        Contents_Iterator next_line_start_iterator = *line_start_iterator;
        backward_line(buffer, &next_line_start_iterator);
        if (next_line_start_iterator.position == line_start_iterator->position) {
            CZ_DEBUG_ASSERT(line_start_iterator->position == 0);
            break;
        }

        int line_rows =
            (line_start_iterator->position - next_line_start_iterator.position + count_cols - 1) /
            count_cols;
        *line_start_iterator = next_line_start_iterator;

        rows += line_rows;
    }
}

static bool add_window_cache_check_point(Window_Cache* window_cache,
                                         Buffer* buffer,
                                         // TODO: Convert this to Contents_Iterator*
                                         uint64_t position,
                                         uint64_t state,
                                         Tokenizer_Check_Point* check_point) {
    Contents_Iterator iterator = buffer->contents.iterator_at(position);
    Token token;
    token.end = position;
    uint64_t contents_len = buffer->contents.len;
    while (token.end <= contents_len) {
        if (token.end >= position + 1024) {
            check_point->position = token.end;
            check_point->state = state;
            return true;
        }

        if (!buffer->mode.next_token(&buffer->contents, &iterator, &token, &state)) {
            break;
        }
    }

    return false;
}

static int cache_windows_check_points(Window_Cache* window_cache, Window* window, Editor* editor) {
    switch (window->tag) {
    case Window::UNIFIED:
        while (1) {
            // TODO: Make this non blocking!
            WITH_BUFFER(buffer, window->v.unified.id, {
                cz::Vector<Tokenizer_Check_Point>* check_points =
                    &window_cache->v.unified.tokenizer_check_points;

                Tokenizer_Check_Point starting_point;
                if (check_points->len() > 0) {
                    starting_point = check_points->last();
                } else {
                    starting_point = {};
                }

                while (1) {
                    int getch_result = getch();
                    if (getch_result != ERR) {
                        return getch_result;
                    }

                    Tokenizer_Check_Point check_point;
                    if (!add_window_cache_check_point(window_cache, buffer, starting_point.position,
                                                      starting_point.state, &check_point)) {
                        break;
                    }
                    starting_point = check_point;
                    check_points->reserve(cz::heap_allocator(), 1);
                    check_points->push(check_point);
                }
            });
        }
        return ERR;

    case Window::VERTICAL_SPLIT: {
        int left_result = cache_windows_check_points(window_cache->v.vertical_split.left,
                                                     window->v.vertical_split.left, editor);
        if (left_result != ERR) {
            return left_result;
        }
        return cache_windows_check_points(window_cache->v.vertical_split.right,
                                          window->v.vertical_split.right, editor);
    }

    case Window::HORIZONTAL_SPLIT: {
        int top_result = cache_windows_check_points(window_cache->v.horizontal_split.top,
                                                    window->v.horizontal_split.top, editor);
        if (top_result != ERR) {
            return top_result;
        }
        return cache_windows_check_points(window_cache->v.horizontal_split.bottom,
                                          window->v.horizontal_split.bottom, editor);
    }
    }

    CZ_PANIC("");
}

static void cache_window_unified_position(Window_Cache* window_cache,
                                          uint64_t start_position,
                                          int count_rows,
                                          int count_cols,
                                          Buffer* buffer) {
    Contents_Iterator visible_end_iterator = buffer->contents.iterator_at(start_position);
    compute_visible_end(buffer, &visible_end_iterator, count_rows, count_cols);
    window_cache->v.unified.visible_end = visible_end_iterator.position;

    cz::Vector<Tokenizer_Check_Point>* check_points =
        &window_cache->v.unified.tokenizer_check_points;

    Tokenizer_Check_Point starting_point;
    if (check_points->len() > 0) {
        starting_point = check_points->last();
    } else {
        starting_point = {};
    }

    while (starting_point.position <= start_position) {
        Tokenizer_Check_Point check_point;
        if (!add_window_cache_check_point(window_cache, buffer, starting_point.position,
                                          starting_point.state, &check_point)) {
            break;
        }
        starting_point = check_point;
        check_points->reserve(cz::heap_allocator(), 1);
        check_points->push(check_point);
    }
}

static void cache_window_unified_update(Window_Cache* window_cache,
                                        Window* window,
                                        Buffer* buffer) {
    cz::Slice<Change> changes = buffer->changes;
    cz::Slice<Tokenizer_Check_Point> check_points = window_cache->v.unified.tokenizer_check_points;
    unsigned char* changed_check_points =
        (unsigned char*)calloc(1, cz::bit_array::alloc_size(check_points.len - 1));
    CZ_DEFER(free(changed_check_points));
    for (size_t i = 0; i < check_points.len; ++i) {
        uint64_t pos = check_points[i].position;
        for (size_t c = window_cache->v.unified.change_index; c < changes.len; ++c) {
            changes[c].adjust_position(&pos);
        }

        if (check_points[i].position != pos) {
            cz::bit_array::set(changed_check_points, i);
        }

        check_points[i].position = pos;
    }
    window_cache->v.unified.change_index = changes.len;

    Token token;
    token.end = 0;
    uint64_t state = 0;
    for (size_t i = 0; i < check_points.len; ++i) {
        uint64_t end_position = check_points[i].position;
        if (cz::bit_array::get(changed_check_points, i)) {
            Contents_Iterator iterator = buffer->contents.iterator_at(token.end);
            while (i < check_points.len) {
                while (token.end < end_position) {
                    if (!buffer->mode.next_token(&buffer->contents, &iterator, &token, &state)) {
                        break;
                    }
                }

                if (token.end > end_position || state != check_points[i].state) {
                    check_points[i].position = token.end;
                    check_points[i].state = state;
                    end_position = check_points[i + 1].position;
                    ++i;
                    if (i == check_points.len) {
                        goto done;
                    }
                } else {
                    break;
                }
            }
        }

        token.end = check_points[i].position;
        state = check_points[i].state;
    }

done:
    cache_window_unified_position(window_cache, window->v.unified.start_position, window->rows,
                                  window->cols, buffer);
}

static void cache_window_unified_create(Window_Cache* window_cache,
                                        Window* window,
                                        Buffer* buffer) {
    window_cache->tag = Window::UNIFIED;
    window_cache->v.unified.id = buffer->id;
    window_cache->v.unified.tokenizer_check_points = {};
    cache_window_unified_update(window_cache, window, buffer);
}

static void cache_window_unified_create(Editor* editor,
                                        Window_Cache* window_cache,
                                        Window* window) {
    WITH_BUFFER(buffer, window->v.unified.id,
                { cache_window_unified_create(window_cache, window, buffer); });
}

static void draw_buffer_contents(Cell* cells,
                                 Window_Cache* window_cache,
                                 int total_cols,
                                 Editor* editor,
                                 Buffer* buffer,
                                 uint64_t* start_position_,
                                 bool show_cursors,
                                 int start_row,
                                 int start_col,
                                 int count_rows,
                                 int count_cols) {
    Contents_Iterator iterator = buffer->contents.iterator_at(*start_position_);
    start_of_line(buffer, &iterator);
    if (window_cache) {
        uint64_t selected_cursor_position = buffer->cursors[0].point;
        Contents_Iterator second_line_iterator = iterator;
        forward_line(buffer, &second_line_iterator);
        if (selected_cursor_position < second_line_iterator.position) {
            iterator = buffer->contents.iterator_at(selected_cursor_position);
            start_of_line(buffer, &iterator);
            backward_line(buffer, &iterator);
            cache_window_unified_position(window_cache, iterator.position, count_rows, count_cols,
                                          buffer);
        } else if (selected_cursor_position >= window_cache->v.unified.visible_end) {
            iterator = buffer->contents.iterator_at(selected_cursor_position);
            start_of_line(buffer, &iterator);
            compute_visible_start(buffer, &iterator, count_rows, count_cols);
            cache_window_unified_position(window_cache, iterator.position, count_rows, count_cols,
                                          buffer);
        }
    }
    *start_position_ = iterator.position;

    int y = 0;
    int x = 0;

    Token token = {};
    uint64_t state = 0;
    bool has_token = true;
    if (window_cache) {
        cz::Slice<Tokenizer_Check_Point> check_points =
            window_cache->v.unified.tokenizer_check_points;
        size_t start = 0;
        size_t end = check_points.len;
        while (start < end) {
            size_t mid = (start + end) / 2;
            if (check_points[mid].position == iterator.position) {
                token.end = check_points[mid].position;
                state = check_points[mid].state;
                break;
            } else if (check_points[mid].position < iterator.position) {
                token.end = check_points[mid].position;
                state = check_points[mid].state;
                start = mid + 1;
            } else {
                end = mid;
            }
        }
    }

    int show_mark = 0;

    for (Contents_Iterator token_iterator = buffer->contents.iterator_at(token.end);
         !iterator.at_eob(); iterator.advance()) {
        while (has_token && iterator.position >= token.end) {
            has_token = buffer->mode.next_token(&buffer->contents, &token_iterator, &token, &state);
        }

        bool has_cursor = false;
        if (show_cursors) {
            for (size_t c = 0; c < buffer->cursors.len(); ++c) {
                Cursor* cursor = &buffer->cursors[c];
                if (buffer->show_marks) {
                    if (iterator.position == std::min(cursor->mark, cursor->point)) {
                        ++show_mark;
                    }
                    if (iterator.position == std::max(cursor->mark, cursor->point)) {
                        --show_mark;
                    }
                }
                if (iterator.position == cursor->point) {
                    has_cursor = true;
                }
            }
        }

#if 0
        if (iterator.index == 0) {
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
        if (has_token && iterator.position >= token.start && iterator.position < token.end) {
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

        char ch = iterator.get();
        if (ch == '\n') {
            ADDCH(attrs, ' ');
            ADD_NEWLINE();
        } else {
            ADDCH(attrs, ch);
        }
    }

    if (show_cursors) {
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            if (buffer->cursors[c].point == buffer->contents.len) {
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
                        Window_Cache* window_cache,
                        int total_cols,
                        Editor* editor,
                        Buffer_Id buffer_id,
                        uint64_t* start_position,
                        bool show_cursors,
                        int start_row,
                        int start_col,
                        int count_rows,
                        int count_cols) {
    WITH_BUFFER(buffer, buffer_id, {
        draw_buffer_contents(cells, window_cache, total_cols, editor, buffer, start_position,
                             show_cursors, start_row, start_col, count_rows - 1, count_cols);

        int y = count_rows - 1;
        int x = 0;

        int attrs = A_REVERSE;
        SET(attrs, '-');
        ++x;
        if (buffer->is_unchanged()) {
            SET(attrs, '-');
        } else {
            SET(attrs, '*');
        }
        ++x;
        SET(attrs, '-');
        ++x;
        SET(attrs, ' ');
        ++x;
        size_t max = cz::min<size_t>(buffer->path.len(), count_cols - x);
        size_t i;
        for (i = 0; i < max; ++i) {
            SET(attrs, buffer->path[i]);
            ++x;
        }
        for (; x < count_cols; ++x) {
            SET(attrs, ' ');
        }
    });
}

static void draw_window(Cell* cells,
                        Window_Cache** window_cache,
                        int total_cols,
                        Editor* editor,
                        Window* window,
                        Window* selected_window,
                        int start_row,
                        int start_col,
                        int count_rows,
                        int count_cols) {
    window->rows = count_rows;
    window->cols = count_cols;

    switch (window->tag) {
    case Window::UNIFIED:
        if (!*window_cache) {
            *window_cache = (Window_Cache*)malloc(sizeof(Window_Cache));
            cache_window_unified_create(editor, *window_cache, window);
        } else if ((*window_cache)->tag != window->tag) {
            destroy_window_cache_children(*window_cache);
            cache_window_unified_create(editor, *window_cache, window);
        } else if ((*window_cache)->v.unified.id != window->v.unified.id) {
            cache_window_unified_create(editor, *window_cache, window);
        } else {
            WITH_BUFFER(buffer, window->v.unified.id, {
                if ((*window_cache)->v.unified.change_index != buffer->changes.len()) {
                    cache_window_unified_update(*window_cache, window, buffer);
                }
            });
        }

        draw_buffer(cells, *window_cache, total_cols, editor, window->v.unified.id,
                    &window->v.unified.start_position, window == selected_window, start_row,
                    start_col, count_rows, count_cols);
        break;

    case Window::VERTICAL_SPLIT: {
        if (!*window_cache) {
            *window_cache = (Window_Cache*)malloc(sizeof(Window_Cache));
            (*window_cache)->tag = Window::VERTICAL_SPLIT;
            (*window_cache)->v.vertical_split = {};
        } else if ((*window_cache)->tag != window->tag) {
            destroy_window_cache_children(*window_cache);
            (*window_cache)->tag = Window::VERTICAL_SPLIT;
            (*window_cache)->v.vertical_split = {};
        }

        int left_cols = (count_cols - 1) / 2;
        int right_cols = count_cols - left_cols - 1;

        draw_window(cells, window_cache, total_cols, editor, window->v.vertical_split.left,
                    selected_window, start_row, start_col, count_rows, left_cols);

        {
            int x = left_cols;
            for (int y = 0; y < count_rows; ++y) {
                SET(A_NORMAL, '|');
            }
        }

        draw_window(cells, window_cache, total_cols, editor, window->v.vertical_split.right,
                    selected_window, start_row, start_col + count_cols - right_cols, count_rows,
                    right_cols);
        break;
    }

    case Window::HORIZONTAL_SPLIT: {
        if (!*window_cache) {
            *window_cache = (Window_Cache*)malloc(sizeof(Window_Cache));
            (*window_cache)->tag = Window::HORIZONTAL_SPLIT;
            (*window_cache)->v.horizontal_split = {};
        } else if ((*window_cache)->tag != window->tag) {
            destroy_window_cache_children(*window_cache);
            (*window_cache)->tag = Window::HORIZONTAL_SPLIT;
            (*window_cache)->v.horizontal_split = {};
        }

        int top_rows = (count_rows - 1) / 2;
        int bottom_rows = count_rows - top_rows - 1;

        draw_window(cells, window_cache, total_cols, editor, window->v.horizontal_split.top,
                    selected_window, start_row, start_col, top_rows, count_cols);

        {
            int y = top_rows;
            for (int x = 0; x < count_cols; ++x) {
                SET(A_NORMAL, '-');
            }
        }

        draw_window(cells, window_cache, total_cols, editor, window->v.horizontal_split.bottom,
                    selected_window, start_row + count_rows - bottom_rows, start_col, bottom_rows,
                    count_cols);
        break;
    }
    }
}

static void render_to_cells(Cell* cells,
                            Window_Cache** window_cache,
                            int total_rows,
                            int total_cols,
                            Editor* editor,
                            Client* client) {
    draw_window(cells, window_cache, total_cols, editor, client->window, client->_selected_window,
                0, 0, total_rows - (client->_message.tag != Message::NONE), total_cols);

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
                uint64_t start_line = 0;
                draw_buffer_contents(cells, nullptr, total_cols, editor, buffer, &start_line,
                                     client->_select_mini_buffer, start_row, start_col,
                                     total_rows - start_row, total_cols - start_col);
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
                   Window_Cache** window_cache,
                   Editor* editor,
                   Client* client) {
    int rows, cols;
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
            cellss[0][i].attrs = 0;
            cellss[0][i].code = ' ';
            cellss[1][i].attrs = 0;
            cellss[1][i].code = ' ';
        }
    }

    render_to_cells(cellss[1], window_cache, rows, cols, editor, client);

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
    CZ_DEFER(endwin());

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

    Window_Cache* window_cache = nullptr;
    CZ_DEFER(destroy_window_cache(window_cache));

    int total_rows = 0;
    int total_cols = 0;
    render(&total_rows, &total_cols, cellss, &window_cache, &server->editor, client);
    nodelay(stdscr, TRUE);

    FILE* file = fopen("tmp.txt", "w");
    CZ_DEFER(fclose(file));

    while (1) {
        int ch = getch();
        if (ch == ERR) {
            render(&total_rows, &total_cols, cellss, &window_cache, &server->editor, client);

            ch = cache_windows_check_points(window_cache, client->window, &server->editor);
            if (ch == ERR) {
                nodelay(stdscr, FALSE);
                continue;
            }
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
}

}
