#include "render.hpp"

#include <Tracy.hpp>
#include "command_macros.hpp"
#include "movement.hpp"
#include "overlay.hpp"
#include "token.hpp"
#include "visible_region.hpp"

namespace mag {
namespace client {

#define SET(FACE, CH)                                                        \
    do {                                                                     \
        Cell* cell = &cells[(y + start_row) * total_cols + (x + start_col)]; \
        cell->face = FACE;                                                   \
        cell->code = CH;                                                     \
    } while (0)

#define ADD_NEWLINE(FACE)               \
    do {                                \
        for (; x < window->cols; ++x) { \
            SET(FACE, ' ');             \
        }                               \
        ++y;                            \
        x = 0;                          \
        if (y == window->rows) {        \
            return;                     \
        }                               \
    } while (0)

#define ADDCH(FACE, CH)          \
    do {                         \
        SET(FACE, CH);           \
        ++x;                     \
        if (x == window->cols) { \
            ADD_NEWLINE({});     \
        }                        \
    } while (0)

static void apply_face(Face* face, Face layer) {
    face->flags |= layer.flags;
    if (layer.foreground != -1 && face->foreground == -1) {
        face->foreground = layer.foreground;
    }
    if (layer.background != -1 && face->background == -1) {
        face->background = layer.background;
    }
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
    buffer->token_cache.update(buffer);
    {
        Tokenizer_Check_Point check_point;
        if (buffer->token_cache.find_check_point(iterator.position, &check_point)) {
            token.end = check_point.position;
            state = check_point.state;
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

    CZ_DEFER({
        for (size_t i = 0; i < editor->theme.overlays.len; ++i) {
            Overlay* overlay = &editor->theme.overlays[i];
            overlay->end_frame();
        }
        for (size_t i = 0; i < buffer->mode.overlays.len; ++i) {
            Overlay* overlay = &buffer->mode.overlays[i];
            overlay->end_frame();
        }
    });

    for (size_t i = 0; i < editor->theme.overlays.len; ++i) {
        Overlay* overlay = &editor->theme.overlays[i];
        overlay->start_frame(buffer, window, iterator);
    }
    for (size_t i = 0; i < buffer->mode.overlays.len; ++i) {
        Overlay* overlay = &buffer->mode.overlays[i];
        overlay->start_frame(buffer, window, iterator);
    }

    Contents_Iterator token_iterator = buffer->contents.iterator_at(token.end);
    for (; !iterator.at_eob(); iterator.advance()) {
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

        Face face = {};
        if (has_cursor) {
            apply_face(&face, editor->theme.faces[2]);
        }

        if (show_mark) {
            apply_face(&face, editor->theme.faces[3]);
        }

        {
            size_t j = 0;
            for (size_t i = 0; i < editor->theme.overlays.len; ++i, ++j) {
                Overlay* overlay = &editor->theme.overlays[i];
                Face overlay_face = overlay->get_face_and_advance(buffer, window, iterator);
                apply_face(&face, overlay_face);
            }
            for (size_t i = 0; i < buffer->mode.overlays.len; ++i, ++j) {
                Overlay* overlay = &buffer->mode.overlays[i];
                Face overlay_face = overlay->get_face_and_advance(buffer, window, iterator);
                apply_face(&face, overlay_face);
            }
        }

        size_t type_face = 6;
        if (has_token && iterator.position >= token.start && iterator.position < token.end) {
            type_face += token.type;
        } else {
            type_face += Token_Type::DEFAULT;
        }
        apply_face(&face, editor->theme.faces[type_face]);

        if (face.flags & Face::INVISIBLE) {
            // Skip rendering this character as it is invisible
            continue;
        }

        char ch = iterator.get();
        if (ch == '\n') {
            SET(face, ' ');
            ++x;

            // Draw newline padding with faces from overlays
            {
                Face face;
                size_t j = 0;
                for (size_t i = 0; i < editor->theme.overlays.len; ++i, ++j) {
                    Overlay* overlay = &editor->theme.overlays[i];
                    Face overlay_face = overlay->get_face_newline_padding(buffer, window, iterator);
                    apply_face(&face, overlay_face);
                }
                for (size_t i = 0; i < buffer->mode.overlays.len; ++i, ++j) {
                    Overlay* overlay = &buffer->mode.overlays[i];
                    Face overlay_face = overlay->get_face_newline_padding(buffer, window, iterator);
                    apply_face(&face, overlay_face);
                }
                ADD_NEWLINE(face);
            }
        } else if (ch == '\t') {
            size_t end_x = (x + 4) & ~3;
            while (x < end_x) {
                ADDCH(face, ' ');
            }
        } else if (isprint(ch)) {
            ADDCH(face, ch);
        } else {
            ADDCH(face, '\\');
            ADDCH(face, '[');
            bool already = false;
            if ((ch / 100) % 10) {
                ADDCH(face, (ch / 100) % 10 + '0');
                already = true;
            }
            if ((ch / 10) % 10 || already) {
                ADDCH(face, (ch / 10) % 10 + '0');
            }
            ADDCH(face, ch % 10 + '0');
            ADDCH(face, ';');
        }
    }

    if (show_cursors) {
        Face face = {};
        apply_face(&face, editor->theme.faces[2]);
        for (size_t c = 0; c < cursors.len; ++c) {
            if (cursors[c].point == buffer->contents.len) {
                SET(face, ' ');
                ++x;
                break;
            }
        }
    }

    for (; y < window->rows; ++y) {
        for (; x < window->cols; ++x) {
            SET({}, ' ');
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

    Face face = {};
    apply_face(&face, editor->theme.faces[buffer->is_unchanged() ? 0 : 1]);

    SET(face, '-');
    ++x;
    if (buffer->is_unchanged()) {
        SET(face, '-');
    } else {
        SET(face, '*');
    }
    ++x;
    SET(face, '-');
    ++x;
    SET(face, ' ');
    ++x;
    size_t max = cz::min<size_t>(buffer->path.len(), window->cols - x);
    for (size_t i = 0; i < max; ++i) {
        SET(face, buffer->path[i]);
        ++x;
    }
    for (; x < window->cols; ++x) {
        SET(face, ' ');
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
            cache_window_unified_create(editor, *window_cache, window);
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
                    SET({}, '|');
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
                    SET({}, '-');
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

        Completion_Cache* completion_cache = &client->mini_buffer_completion_cache;
        if (client->_message.tag > Message::SHOW) {
            {
                Window_Unified* window = client->mini_buffer_window();
                WITH_WINDOW_BUFFER(window);
                if (completion_cache->change_index != buffer->changes.len() ||
                    completion_cache->engine != client->_message.completion_engine) {
                    // Restart completion as mini buffer changed.
                    completion_cache->change_index = buffer->changes.len();
                    completion_cache->results.state = Completion_Results::INITIAL;
                    completion_cache->results.query.set_len(0);
                    buffer->contents.stringify_into(cz::heap_allocator(),
                                                    &completion_cache->results.query);
                }
            }

            switch (completion_cache->results.state) {
            case Completion_Results::INITIAL:
                if (completion_cache->engine != client->_message.completion_engine) {
                    completion_cache->engine = client->_message.completion_engine;
                    if (completion_cache->results.cleanup) {
                        completion_cache->results.cleanup(completion_cache->results.data);
                    }
                    completion_cache->results.cleanup = nullptr;
                    completion_cache->results.data = nullptr;
                    completion_cache->results.results_buffer_array.clear();
                    completion_cache->results.results.set_len(0);
                    completion_cache->results.selected = 0;
                }
                break;

            case Completion_Results::LOADING:
                break;

            case Completion_Results::LOADED:
                results_height = completion_cache->results.results.len();
                if (results_height > editor->theme.max_completion_results) {
                    results_height = editor->theme.max_completion_results;
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

        Face minibuffer_prompt_face = {};
        apply_face(&minibuffer_prompt_face, editor->theme.faces[4]);
        for (size_t i = 0; i < client->_message.text.len && i < total_cols; ++i) {
            SET(minibuffer_prompt_face, client->_message.text[i]);
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
                SET({}, ' ');
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
            size_t offset = completion_cache->results.selected;
            if (offset >= completion_cache->results.results.len() - results_height / 2) {
                offset = completion_cache->results.results.len() - results_height;
            } else if (offset < results_height / 2) {
                offset = 0;
            } else {
                offset -= results_height / 2;
            }
            for (size_t r = offset; r < results_height + offset; ++r) {
                {
                    Face face = {};
                    if (r == completion_cache->results.selected) {
                        apply_face(&face, editor->theme.faces[5]);
                    }

                    cz::Str result = completion_cache->results.results[r];
                    for (size_t i = 0; i < total_cols && i < result.len; ++i) {
                        SET(face, result[i]);
                        ++x;
                    }
                }

                for (; x < total_cols; ++x) {
                    SET({}, ' ');
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
