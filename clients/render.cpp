#include "render.hpp"

#include <ncurses.h>
#include <Tracy.hpp>
#include "command_macros.hpp"
#include "movement.hpp"
#include "token.hpp"
#include "visible_region.hpp"

namespace mag {
namespace client {

#define SET(ATTRS, CH)                                                       \
    do {                                                                     \
        Cell* cell = &cells[(y + start_row) * total_cols + (x + start_col)]; \
        cell->attrs = ATTRS;                                                 \
        cell->code = CH;                                                     \
    } while (0)

#define ADD_NEWLINE()                   \
    do {                                \
        for (; x < window->cols; ++x) { \
            SET(A_NORMAL, ' ');         \
        }                               \
        ++y;                            \
        x = 0;                          \
        if (y == window->rows) {        \
            return;                     \
        }                               \
    } while (0)

#define ADDCH(ATTRS, CH)         \
    do {                         \
        SET(ATTRS, CH);          \
        ++x;                     \
        if (x == window->cols) { \
            ADD_NEWLINE();       \
        }                        \
    } while (0)

static bool apply_face(int* attrs, Face* face) {
    if (face->flags & Face::BOLD) {
        *attrs |= A_BOLD;
    }
    if (face->flags & Face::UNDERSCORE) {
        *attrs |= A_UNDERLINE;
    }
    if (face->flags & Face::REVERSE) {
        *attrs |= A_REVERSE;
    }
    return face->foreground < SIZE_MAX || face->background < SIZE_MAX;
}

static void draw_buffer_contents(Cell* cells,
                                 Window_Cache* window_cache,
                                 size_t total_cols,
                                 Editor* editor,
                                 Buffer* buffer,
                                 Window_Unified* window,
                                 bool show_cursors,
                                 size_t start_row,
                                 size_t start_col) {
    ZoneScoped;

    Contents_Iterator iterator = buffer->contents.iterator_at(window->start_position);
    start_of_line(&iterator);
    if (window_cache) {
        if (window->start_position != window_cache->v.unified.visible_start) {
            // Start position updated in a command so recalculate end position.
            Contents_Iterator visible_end_iterator =
                buffer->contents.iterator_at(window->start_position);
            compute_visible_end(window, &visible_end_iterator);
            window_cache->v.unified.visible_start = window->start_position;
            window_cache->v.unified.visible_end = visible_end_iterator.position;
        }

        // Ensure the cursor is visible
        uint64_t selected_cursor_position = window->cursors[0].point;
        Contents_Iterator second_visible_line_iterator = iterator;
        forward_line(&second_visible_line_iterator);
        if (selected_cursor_position < second_visible_line_iterator.position) {
            // We are above the second visible line and thus readjust
            iterator = buffer->contents.iterator_at(selected_cursor_position);
            start_of_line(&iterator);
            backward_line(&iterator);
            cache_window_unified_position(window, window_cache, iterator.position, buffer);
        } else if (selected_cursor_position > window_cache->v.unified.visible_end) {
            // We are below the "visible" section of the buffer ie on the last line or beyond the
            // last line.
            iterator = buffer->contents.iterator_at(selected_cursor_position);
            start_of_line(&iterator);
            forward_line(&iterator);
            compute_visible_start(window, &iterator);
            cache_window_unified_position(window, window_cache, iterator.position, buffer);
        }
    }
    window->start_position = iterator.position;

    size_t y = 0;
    size_t x = 0;

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

    cz::Slice<Cursor> cursors = window->cursors;

    int show_mark = 0;
    // Initialize show_mark to number of regions at start of visible region.
    if (show_cursors && window->show_marks) {
        for (size_t c = 0; c < cursors.len; ++c) {
            if (iterator.position > cursors[c].start() && iterator.position < cursors[c].end()) {
                ++show_mark;
            }
        }
    }

    for (Contents_Iterator token_iterator = buffer->contents.iterator_at(token.end);
         !iterator.at_eob(); iterator.advance()) {
        while (has_token && iterator.position >= token.end) {
            has_token = buffer->mode.next_token(&token_iterator, &token, &state);
        }

        bool has_cursor = false;
        if (show_cursors) {
            for (size_t c = 0; c < cursors.len; ++c) {
                if (window->show_marks) {
                    if (iterator.position == cursors[c].start()) {
                        ++show_mark;
                    }
                    if (iterator.position == cursors[c].end()) {
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
        int color_face = 0;
        if (has_cursor) {
            int cursor_face = 2;
            if (apply_face(&attrs, &editor->theme.faces[cursor_face]) && color_face == 0) {
                color_face = cursor_face;
            }
        }

        if (show_mark) {
            int mark_face = 3;
            if (apply_face(&attrs, &editor->theme.faces[mark_face]) && color_face == 0) {
                color_face = mark_face;
            }
        }

        int type_face = 5;
        if (has_token && iterator.position >= token.start && iterator.position < token.end) {
            type_face += token.type;
        } else {
            type_face += Token_Type::DEFAULT;
        }

        if (apply_face(&attrs, &editor->theme.faces[type_face]) && color_face == 0) {
            color_face = type_face;
        }

        if (color_face != 0) {
            attrs |= COLOR_PAIR(color_face + 1);
        }

        char ch = iterator.get();
        if (ch == '\n') {
            SET(attrs, ' ');
            ++x;
            ADD_NEWLINE();
        } else if (ch == '\t') {
            size_t end_x = (x + 4) & ~3;
            while (x < end_x) {
                ADDCH(attrs, ' ');
            }
        } else {
            ADDCH(attrs, ch);
        }
    }

    if (show_cursors) {
        for (size_t c = 0; c < cursors.len; ++c) {
            if (cursors[c].point == buffer->contents.len) {
                SET(A_REVERSE, ' ');
                ++x;
                break;
            }
        }
    }

    for (; y < window->rows; ++y) {
        for (; x < window->cols; ++x) {
            SET(A_NORMAL, ' ');
        }
        x = 0;
    }
}

static void draw_buffer_decoration(Cell* cells,
                                   size_t total_cols,
                                   Editor* editor,
                                   Window* window,
                                   Buffer* buffer,
                                   size_t start_row,
                                   size_t start_col) {
    ZoneScoped;

    size_t y = window->rows;
    size_t x = 0;

    int attrs = A_NORMAL;
    if (buffer->is_unchanged()) {
        if (apply_face(&attrs, &editor->theme.faces[0])) {
            attrs |= COLOR_PAIR(1);
        }
    } else {
        if (apply_face(&attrs, &editor->theme.faces[1])) {
            attrs |= COLOR_PAIR(2);
        }
    }

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
    size_t max = cz::min<size_t>(buffer->path.len(), window->cols - x);
    for (size_t i = 0; i < max; ++i) {
        SET(attrs, buffer->path[i]);
        ++x;
    }
    for (; x < window->cols; ++x) {
        SET(attrs, ' ');
    }
}

static void draw_buffer(Cell* cells,
                        Window_Cache* window_cache,
                        size_t total_cols,
                        Editor* editor,
                        Window_Unified* window,
                        bool show_cursors,
                        size_t start_row,
                        size_t start_col) {
    ZoneScoped;

    WITH_WINDOW_BUFFER(window);
    draw_buffer_contents(cells, window_cache, total_cols, editor, buffer, window, show_cursors,
                         start_row, start_col);
    draw_buffer_decoration(cells, total_cols, editor, window, buffer, start_row, start_col);
}

static void draw_window(Cell* cells,
                        Window_Cache** window_cache,
                        size_t total_cols,
                        Editor* editor,
                        Window* w,
                        Window* selected_window,
                        size_t start_row,
                        size_t start_col,
                        size_t count_rows,
                        size_t count_cols) {
    ZoneScoped;

    w->rows = count_rows - 1;
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
                WITH_WINDOW_BUFFER(window);
                if ((*window_cache)->v.unified.change_index != buffer->changes.len()) {
                    cache_window_unified_update(*window_cache, window, buffer);
                }
            }
        }

        draw_buffer(cells, *window_cache, total_cols, editor, window, window == selected_window,
                    start_row, start_col);
        break;
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)w;

        if (!*window_cache) {
            *window_cache = (Window_Cache*)malloc(sizeof(Window_Cache));
            (*window_cache)->tag = window->tag;
            (*window_cache)->v.split = {};
        } else if ((*window_cache)->tag != window->tag) {
            destroy_window_cache_children(*window_cache);
            (*window_cache)->tag = window->tag;
            (*window_cache)->v.split = {};
        }

        if (window->tag == Window::VERTICAL_SPLIT) {
            size_t left_cols = (count_cols - 1) / 2;
            size_t right_cols = count_cols - left_cols - 1;

            draw_window(cells, &(*window_cache)->v.split.first, total_cols, editor, window->first,
                        selected_window, start_row, start_col, count_rows, left_cols);

            {
                size_t x = left_cols;
                for (size_t y = 0; y < count_rows; ++y) {
                    SET(A_NORMAL, '|');
                }
            }

            draw_window(cells, &(*window_cache)->v.split.second, total_cols, editor, window->second,
                        selected_window, start_row, start_col + count_cols - right_cols, count_rows,
                        right_cols);
        } else {
            size_t top_rows = (count_rows - 1) / 2;
            size_t bottom_rows = count_rows - top_rows - 1;

            draw_window(cells, &(*window_cache)->v.split.first, total_cols, editor, window->first,
                        selected_window, start_row, start_col, top_rows, count_cols);

            {
                size_t y = top_rows;
                for (size_t x = 0; x < count_cols; ++x) {
                    SET(A_NORMAL, '-');
                }
            }

            draw_window(cells, &(*window_cache)->v.split.second, total_cols, editor, window->second,
                        selected_window, start_row + count_rows - bottom_rows, start_col,
                        bottom_rows, count_cols);
        }
        break;
    }
    }
}

void render_to_cells(Cell* cells,
                     Window_Cache** window_cache,
                     size_t total_rows,
                     size_t total_cols,
                     Editor* editor,
                     Client* client) {
    ZoneScoped;

    size_t mini_buffer_height = 0;

    if (client->_message.tag != Message::NONE) {
        ZoneScopedN("Draw mini buffer");

        mini_buffer_height = 1;
        size_t results_height = 0;

        Completion_Results* completion_results = &client->mini_buffer_completion_results;
        if (client->_message.tag > Message::SHOW) {
            {
                Window_Unified* window = client->mini_buffer_window();
                WITH_WINDOW_BUFFER(window);
                if (completion_results->change_index != buffer->changes.len()) {
                    completion_results->change_index = buffer->changes.len();
                    completion_results->state = Completion_Results::INITIAL;
                    completion_results->query.set_len(0);
                    buffer->contents.stringify_into(cz::heap_allocator(),
                                                    &completion_results->query);
                }
            }

            switch (completion_results->state) {
            case Completion_Results::INITIAL:
                completion_results->results.set_len(0);
                completion_results->state = Completion_Results::LOADING;
                completion_results->selected = 0;
                break;

            case Completion_Results::LOADING:
                break;

            case Completion_Results::LOADED:
                results_height = completion_results->results.len();
                if (results_height > 5) {
                    results_height = 5;
                }
                if (results_height > total_rows / 2) {
                    results_height = total_rows / 2;
                }
                break;
            }
        }

        size_t y = 0;
        size_t x = 0;
        size_t start_row = total_rows - mini_buffer_height - results_height;
        size_t start_col = 0;
        int attrs = A_NORMAL;

        for (size_t i = 0; i < client->_message.text.len && i < total_cols; ++i) {
            SET(attrs, client->_message.text[i]);
            ++x;
        }

        if (client->_message.tag > Message::SHOW) {
            start_col = x;
            Window_Unified* window = client->mini_buffer_window();
            WITH_WINDOW_BUFFER(window);
            window->rows = mini_buffer_height;
            window->cols = total_cols - start_col;
            draw_buffer_contents(cells, nullptr, total_cols, editor, buffer, window,
                                 client->_select_mini_buffer, start_row, start_col);
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
            size_t y = 0;
            size_t x = 0;
            size_t start_row = total_rows - results_height;
            size_t start_col = 0;
            size_t offset = completion_results->selected;
            if (offset >= completion_results->results.len() - results_height / 2) {
                offset = completion_results->results.len() - results_height;
            } else if (offset < results_height / 2) {
                offset = 0;
            } else {
                offset -= results_height / 2;
            }
            for (size_t r = offset; r < results_height + offset; ++r) {
                {
                    int attrs = A_NORMAL;
                    if (r == completion_results->selected) {
                        int completion_face = 4;
                        if (!apply_face(&attrs, &editor->theme.faces[completion_face])) {
                            attrs |= COLOR_PAIR(completion_face + 1);
                        }
                    }
                    cz::Str result = completion_results->results[r];
                    for (size_t i = 0; i < total_cols && i < result.len; ++i) {
                        SET(attrs, result[i]);
                        ++x;
                    }
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

}
}
