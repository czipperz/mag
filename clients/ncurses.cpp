#include "ncurses.hpp"

#include <ctype.h>
#include <ncurses.h>
#include <Tracy.hpp>
#include <algorithm>
#include <cz/bit_array.hpp>
#include <cz/fs/directory.hpp>
#include <thread>
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
            bool tokenizer_ran_to_end;
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

struct Mini_Buffer_Results {
    enum State {
        INITIAL,
        LOADING,
        LOADED,
    };

    State state;
    cz::String query;
    cz::Vector<cz::Str> results;
    size_t selected;
    Message::Tag response_tag;
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
    ZoneScoped;

    int rows;
    for (rows = 0; rows < count_rows - 1;) {
        Contents_Iterator next_line_start_iterator = *line_start_iterator;
        forward_line(&next_line_start_iterator);
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
    ZoneScoped;

    for (int rows = 0; rows < count_rows - 2;) {
        Contents_Iterator next_line_start_iterator = *line_start_iterator;
        backward_line(&next_line_start_iterator);
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

static bool next_check_point(Window_Cache* window_cache,
                             Buffer* buffer,
                             Contents_Iterator* iterator,
                             uint64_t* state,
                             cz::Vector<Tokenizer_Check_Point>* check_points) {
    ZoneScoped;

    uint64_t start_position = iterator->position;
    while (!iterator->at_eob()) {
        if (iterator->position >= start_position + 1024) {
            Tokenizer_Check_Point check_point;
            check_point.position = iterator->position;
            check_point.state = *state;
            check_points->reserve(cz::heap_allocator(), 1);
            check_points->push(check_point);
            return true;
        }

        Token token;
        if (!buffer->mode.next_token(&buffer->contents, iterator, &token, state)) {
            break;
        }
    }

    return false;
}

static int cache_windows_check_points(Window_Cache* window_cache, Window* w, Editor* editor) {
    ZoneScoped;

    CZ_DEBUG_ASSERT(window_cache->tag == w->tag);

    switch (w->tag) {
    case Window::UNIFIED: {
        Window_Unified* window = (Window_Unified*)w;

        if (window_cache->v.unified.tokenizer_ran_to_end) {
            return ERR;
        }

        // TODO: Make this non blocking!
        {
            WITH_BUFFER(window->id);
            cz::Vector<Tokenizer_Check_Point>* check_points =
                &window_cache->v.unified.tokenizer_check_points;

            uint64_t state;
            Contents_Iterator iterator;
            if (check_points->len() > 0) {
                state = check_points->last().state;
                iterator = buffer->contents.iterator_at(check_points->last().position);
            } else {
                state = 0;
                iterator = buffer->contents.iterator_at(0);
            }

            while (1) {
                int getch_result = getch();
                if (getch_result != ERR) {
                    return getch_result;
                }

                if (!next_check_point(window_cache, buffer, &iterator, &state, check_points)) {
                    window_cache->v.unified.tokenizer_ran_to_end = true;
                    return ERR;
                }
            }
        }
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)w;
        int first_result =
            cache_windows_check_points(window_cache->v.vertical_split.left, window->first, editor);
        if (first_result != ERR) {
            return first_result;
        }
        return cache_windows_check_points(window_cache->v.vertical_split.right, window->second,
                                          editor);
    }
    }

    CZ_PANIC("");
}

static void cache_window_unified_position(Window_Cache* window_cache,
                                          uint64_t start_position,
                                          int count_rows,
                                          int count_cols,
                                          Buffer* buffer) {
    ZoneScoped;

    Contents_Iterator visible_end_iterator = buffer->contents.iterator_at(start_position);
    compute_visible_end(buffer, &visible_end_iterator, count_rows, count_cols);
    window_cache->v.unified.visible_end = visible_end_iterator.position;

    cz::Vector<Tokenizer_Check_Point>* check_points =
        &window_cache->v.unified.tokenizer_check_points;

    uint64_t state;
    Contents_Iterator iterator;
    if (check_points->len() > 0) {
        state = check_points->last().state;
        iterator = buffer->contents.iterator_at(check_points->last().position);
    } else {
        state = 0;
        iterator = buffer->contents.iterator_at(0);
    }

    while (iterator.position <= start_position) {
        if (!next_check_point(window_cache, buffer, &iterator, &state, check_points)) {
            break;
        }
    }
}

static void cache_window_unified_update(Window_Cache* window_cache,
                                        Window_Unified* window,
                                        Buffer* buffer) {
    ZoneScoped;

    cz::Slice<Change> changes = buffer->changes;
    cz::Slice<Tokenizer_Check_Point> check_points = window_cache->v.unified.tokenizer_check_points;
    unsigned char* changed_check_points =
        (unsigned char*)calloc(1, cz::bit_array::alloc_size(check_points.len));
    CZ_DEFER(free(changed_check_points));
    // Detect check points that changed
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
    // Fix check points that were changed
    for (size_t i = 0; i < check_points.len; ++i) {
        uint64_t end_position = check_points[i].position;
        if (cz::bit_array::get(changed_check_points, i)) {
            Contents_Iterator iterator = buffer->contents.iterator_at(token.end);
            // Efficiently loop without recalculating the iterator so long as
            // the edit is screwing up future check points.
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
    cache_window_unified_position(window_cache, window->start_position, window->rows, window->cols,
                                  buffer);
}

static void cache_window_unified_create(Window_Cache* window_cache,
                                        Window_Unified* window,
                                        Buffer_Id buffer_id,
                                        Buffer* buffer) {
    ZoneScoped;

    window_cache->tag = Window::UNIFIED;
    window_cache->v.unified.id = buffer_id;
    window_cache->v.unified.tokenizer_check_points = {};
    window_cache->v.unified.tokenizer_ran_to_end = false;
    cache_window_unified_update(window_cache, window, buffer);
}

static void cache_window_unified_create(Editor* editor,
                                        Window_Cache* window_cache,
                                        Window_Unified* window) {
    {
        WITH_BUFFER(window->id);
        cache_window_unified_create(window_cache, window, window->id, buffer);
    }
}

static void draw_buffer_contents(Cell* cells,
                                 Window_Cache* window_cache,
                                 int total_cols,
                                 Editor* editor,
                                 Buffer* buffer,
                                 Window_Unified* window,
                                 bool show_cursors,
                                 int start_row,
                                 int start_col,
                                 int count_rows,
                                 int count_cols) {
    ZoneScoped;

    window->update_cursors(buffer->changes);

    Contents_Iterator iterator = buffer->contents.iterator_at(window->start_position);
    start_of_line(&iterator);
    if (window_cache) {
        // Ensure the cursor is visible
        uint64_t selected_cursor_position = window->cursors[0].point;
        Contents_Iterator second_line_iterator = iterator;
        forward_line(&second_line_iterator);
        if (selected_cursor_position < second_line_iterator.position) {
            iterator = buffer->contents.iterator_at(selected_cursor_position);
            start_of_line(&iterator);
            backward_line(&iterator);
            cache_window_unified_position(window_cache, iterator.position, count_rows, count_cols,
                                          buffer);
        } else if (selected_cursor_position >= window_cache->v.unified.visible_end) {
            iterator = buffer->contents.iterator_at(selected_cursor_position);
            start_of_line(&iterator);
            compute_visible_start(buffer, &iterator, count_rows, count_cols);
            cache_window_unified_position(window_cache, iterator.position, count_rows, count_cols,
                                          buffer);
        }
    }
    window->start_position = iterator.position;

    int y = 0;
    int x = 0;

    Token token = {};
    uint64_t state = 0;
    bool has_token = true;
    if (window_cache) {
        // Find the last check point before the start position
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
            cz::Slice<Cursor> cursors = window->cursors;
            for (size_t c = 0; c < cursors.len; ++c) {
                if (window->show_marks) {
                    if (iterator.position == std::min(cursors[c].mark, cursors[c].point)) {
                        ++show_mark;
                    }
                    if (iterator.position == std::max(cursors[c].mark, cursors[c].point)) {
                        --show_mark;
                    }
                }
                if (iterator.position == cursors[c].point) {
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
        cz::Slice<Cursor> cursors = window->cursors;
        for (size_t c = 0; c < cursors.len; ++c) {
            if (cursors[c].point == buffer->contents.len) {
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
                        Window_Unified* window,
                        bool show_cursors,
                        int start_row,
                        int start_col,
                        int count_rows,
                        int count_cols) {
    ZoneScoped;

    {
        WITH_BUFFER(window->id);
        draw_buffer_contents(cells, window_cache, total_cols, editor, buffer, window, show_cursors,
                             start_row, start_col, count_rows - 1, count_cols);

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
    }
}

static void draw_window(Cell* cells,
                        Window_Cache** window_cache,
                        int total_cols,
                        Editor* editor,
                        Window* w,
                        Window* selected_window,
                        int start_row,
                        int start_col,
                        int count_rows,
                        int count_cols) {
    ZoneScoped;

    w->rows = count_rows;
    w->cols = count_cols;

    switch (w->tag) {
    case Window::UNIFIED: {
        Window_Unified* window = (Window_Unified*)w;

        if (!*window_cache) {
            *window_cache = (Window_Cache*)malloc(sizeof(Window_Cache));
            cache_window_unified_create(editor, *window_cache, window);
        } else if ((*window_cache)->tag != window->tag) {
            destroy_window_cache_children(*window_cache);
            cache_window_unified_create(editor, *window_cache, window);
        } else if ((*window_cache)->v.unified.id != window->id) {
            (*window_cache)->v.unified.tokenizer_check_points.drop(cz::heap_allocator());
            cache_window_unified_create(editor, *window_cache, window);
        } else {
            {
                WITH_BUFFER(window->id);
                if ((*window_cache)->v.unified.change_index != buffer->changes.len()) {
                    cache_window_unified_update(*window_cache, window, buffer);
                }
            }
        }

        draw_buffer(cells, *window_cache, total_cols, editor, window, window == selected_window,
                    start_row, start_col, count_rows, count_cols);
        break;
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)w;

        if (!*window_cache) {
            *window_cache = (Window_Cache*)malloc(sizeof(Window_Cache));
            (*window_cache)->tag = window->tag;
            (*window_cache)->v.vertical_split = {};
        } else if ((*window_cache)->tag != window->tag) {
            destroy_window_cache_children(*window_cache);
            (*window_cache)->tag = window->tag;
            (*window_cache)->v.vertical_split = {};
        }

        if (window->tag == Window::VERTICAL_SPLIT) {
            int left_cols = (count_cols - 1) / 2;
            int right_cols = count_cols - left_cols - 1;

            draw_window(cells, &(*window_cache)->v.vertical_split.left, total_cols, editor,
                        window->first, selected_window, start_row, start_col, count_rows,
                        left_cols);

            {
                int x = left_cols;
                for (int y = 0; y < count_rows; ++y) {
                    SET(A_NORMAL, '|');
                }
            }

            draw_window(cells, &(*window_cache)->v.vertical_split.right, total_cols, editor,
                        window->second, selected_window, start_row,
                        start_col + count_cols - right_cols, count_rows, right_cols);
        } else {
            int top_rows = (count_rows - 1) / 2;
            int bottom_rows = count_rows - top_rows - 1;

            draw_window(cells, &(*window_cache)->v.horizontal_split.top, total_cols, editor,
                        window->first, selected_window, start_row, start_col, top_rows, count_cols);

            {
                int y = top_rows;
                for (int x = 0; x < count_cols; ++x) {
                    SET(A_NORMAL, '-');
                }
            }

            draw_window(cells, &(*window_cache)->v.horizontal_split.bottom, total_cols, editor,
                        window->second, selected_window, start_row + count_rows - bottom_rows,
                        start_col, bottom_rows, count_cols);
        }
        break;
    }
    }
}

static void render_to_cells(Cell* cells,
                            Window_Cache** window_cache,
                            Mini_Buffer_Results* mini_buffer_results,
                            int total_rows,
                            int total_cols,
                            Editor* editor,
                            Client* client) {
    ZoneScoped;

    int mini_buffer_height = 0;

    if (client->_message.tag != Message::NONE) {
        ZoneScopedN("Draw mini buffer");

        mini_buffer_height = 1;
        int results_height = 0;

        if (mini_buffer_results->response_tag != client->_message.tag) {
            mini_buffer_results->results.set_len(0);
            mini_buffer_results->state = Mini_Buffer_Results::INITIAL;
        }

        if (client->_message.tag > Message::SHOW) {
            {
                WITH_BUFFER(client->mini_buffer_window()->id);
                Contents_Iterator iterator = buffer->contents.iterator_at(0);
                size_t i = 0;
                cz::Str query = mini_buffer_results->query;
                while (1) {
                    if (i == query.len && iterator.at_eob()) {
                        break;
                    } else if (i == query.len || iterator.at_eob()) {
                        mini_buffer_results->state = Mini_Buffer_Results::INITIAL;
                        break;
                    }

                    if (query[i] != iterator.get()) {
                        mini_buffer_results->state = Mini_Buffer_Results::INITIAL;
                        break;
                    }

                    ++i;
                    iterator.advance();
                }
            }

            switch (mini_buffer_results->state) {
            case Mini_Buffer_Results::INITIAL: {
                WITH_BUFFER(client->mini_buffer_window()->id);
                mini_buffer_results->query.set_len(0);
                buffer->contents.stringify_into(cz::heap_allocator(), &mini_buffer_results->query);
            }
                mini_buffer_results->results.set_len(0);
                mini_buffer_results->response_tag = client->_message.tag;
                mini_buffer_results->state = Mini_Buffer_Results::LOADING;
                break;

            case Mini_Buffer_Results::LOADING:
                break;

            case Mini_Buffer_Results::LOADED:
                results_height = mini_buffer_results->results.len();
                if (results_height > 5) {
                    results_height = 5;
                }
                if (results_height > total_rows / 2) {
                    results_height = total_rows / 2;
                }
                break;
            }
        }

        int y = 0;
        int x = 0;
        int start_row = total_rows - mini_buffer_height - results_height;
        int start_col = 0;
        int attrs = A_NORMAL;

        for (size_t i = 0; i < client->_message.text.len && i < total_cols; ++i) {
            SET(attrs, client->_message.text[i]);
            ++x;
        }

        if (client->_message.tag > Message::SHOW) {
            start_col = x;
            Window_Unified* window = client->mini_buffer_window();
            WITH_BUFFER(window->id);
            draw_buffer_contents(cells, nullptr, total_cols, editor, buffer, window,
                                 client->_select_mini_buffer, start_row, start_col,
                                 mini_buffer_height, total_cols - start_col);
        } else {
            for (; x < total_cols; ++x) {
                SET(attrs, ' ');
            }

            if (std::chrono::system_clock::now() - client->_message_time >
                std::chrono::seconds(5)) {
                client->_message.tag = Message::NONE;
            }
        }

        {
            int y = 0;
            int x = 0;
            int start_row = total_rows - results_height;
            int start_col = 0;
            for (int r = 0; r < results_height; ++r) {
                cz::Str result = mini_buffer_results->results[r];
                for (size_t i = 0; i < total_cols && i < result.len; ++i) {
                    SET(attrs, result[i]);
                    ++x;
                }

                for (; x < total_cols; ++x) {
                    SET(attrs, ' ');
                }

                x = 0;
                ++y;
            }
        }

        total_rows = start_row;
    }

    draw_window(cells, window_cache, total_cols, editor, client->window,
                client->selected_normal_window, 0, 0, total_rows, total_cols);
}

static void render(int* total_rows,
                   int* total_cols,
                   Cell** cellss,
                   Window_Cache** window_cache,
                   Mini_Buffer_Results* mini_buffer_results,
                   Editor* editor,
                   Client* client) {
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
                cellss[0][i].attrs = 0;
                cellss[0][i].code = ' ';
                cellss[1][i].attrs = 0;
                cellss[1][i].code = ' ';
            }
        }
    }

    render_to_cells(cellss[1], window_cache, mini_buffer_results, rows, cols, editor, client);

    {
        ZoneScopedN("blit cells");
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
    }

    cz::swap(cellss[0], cellss[1]);

    {
        ZoneScopedN("refresh");
        refresh();
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

static bool binary_search_string_prefix_start(cz::Slice<cz::Str> results,
                                              cz::Str prefix,
                                              size_t* out) {
    size_t start = 0;
    size_t end = results.len;
    while (start < end) {
        size_t mid = (start + end) / 2;
        int cmp =
            memcmp(results[mid].buffer, prefix.buffer, std::min(results[mid].len, prefix.len));
        if (cmp < 0) {
            start = mid + 1;
        } else if (cmp == 0) {
            if (results[mid].len < prefix.len) {
                // Too short means we need to look at longer strings that are sorted after shorter
                // strings.
                start = mid + 1;
            } else if (end > start + 1) {
                // Even though we found one match, we need to look earlier for a lesser match.
                if (mid + 1 >= end) {
                    CZ_DEBUG_ASSERT(mid > 0);
                    if (results[mid - 1].len > prefix.len &&
                        memcmp(results[mid - 1].buffer, prefix.buffer, prefix.len) == 0) {
                        *out = mid - 1;
                    } else {
                        *out = mid;
                    }
                    return true;
                }
                end = mid + 1;
            } else {
                *out = mid;
                return true;
            }
        } else {
            end = mid;
        }
    }
    return false;
}

static size_t binary_search_string_prefix_end(cz::Slice<cz::Str> results,
                                              size_t start,
                                              cz::Str prefix) {
    size_t end = results.len;
    while (start < end) {
        size_t mid = (start + end) / 2;
        int cmp =
            memcmp(results[mid].buffer, prefix.buffer, std::min(results[mid].len, prefix.len));
        if (cmp < 0) {
            CZ_PANIC("Unreachable: sorted list is out of order");
        } else if (cmp == 0) {
            if (end > start + 1) {
                start = mid;
            } else {
                break;
            }
        } else {
            end = mid;
        }
    }
    return end;
}

static int load_mini_buffer_results(Mini_Buffer_Results* mini_buffer_results) {
    ZoneScoped;

    switch (mini_buffer_results->response_tag) {
    case Message::RESPOND_FILE: {
        mini_buffer_results->query.reserve(cz::heap_allocator(), 1);
        mini_buffer_results->query.null_terminate();
        char* dir_sep = mini_buffer_results->query.rfind('/');
        cz::Str prefix;
        if (dir_sep) {
            *dir_sep = '\0';
            cz::fs::files(cz::heap_allocator(), cz::heap_allocator(),
                          mini_buffer_results->query.buffer(), &mini_buffer_results->results);
            *dir_sep = '/';
            prefix = dir_sep + 1;
        } else {
            cz::fs::files(cz::heap_allocator(), cz::heap_allocator(), ".",
                          &mini_buffer_results->results);
            prefix = mini_buffer_results->query;
        }
        std::sort(mini_buffer_results->results.start(), mini_buffer_results->results.end());

        size_t start;
        if (binary_search_string_prefix_start(mini_buffer_results->results, prefix, &start)) {
            size_t end =
                binary_search_string_prefix_end(mini_buffer_results->results, start, prefix);
            mini_buffer_results->results.set_len(end);
            mini_buffer_results->results.remove_range(0, start);
        } else {
            mini_buffer_results->results.set_len(0);
        }

        mini_buffer_results->state = Mini_Buffer_Results::LOADED;
        break;
    }

    case Message::RESPOND_BUFFER:
        mini_buffer_results->state = Mini_Buffer_Results::LOADED;
        break;

    case Message::RESPOND_TEXT:
        mini_buffer_results->state = Mini_Buffer_Results::LOADED;
        break;

    case Message::NONE:
    case Message::SHOW:
        CZ_PANIC("");
        break;
    }

    return ERR;
}

void run_ncurses(Server* server, Client* client) {
    ZoneScoped;

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

    Mini_Buffer_Results mini_buffer_results = {};

    int total_rows = 0;
    int total_cols = 0;

    nodelay(stdscr, TRUE);

    while (1) {
        ZoneScopedN("ncurses main loop");

        render(&total_rows, &total_cols, cellss, &window_cache, &mini_buffer_results,
               &server->editor, client);

        int ch = ERR;
        if (ch == ERR && mini_buffer_results.state == Mini_Buffer_Results::LOADING) {
            ch = load_mini_buffer_results(&mini_buffer_results);
            if (ch == ERR) {
                continue;
            }
        }

        if (ch == ERR) {
            ch = cache_windows_check_points(window_cache, client->window, &server->editor);
        }

        if (ch == ERR) {
            nodelay(stdscr, FALSE);
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
