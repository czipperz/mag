#include "render.hpp"

#include <ctype.h>
#include <Tracy.hpp>
#include "command_macros.hpp"
#include "decoration.hpp"
#include "movement.hpp"
#include "overlay.hpp"
#include "server.hpp"
#include "token.hpp"
#include "visible_region.hpp"

namespace mag {
namespace client {

#define SET_IND(FACE, CH)                                                    \
    do {                                                                     \
        Cell* cell = &cells[(y + start_row) * total_cols + (x + start_col)]; \
        cell->face = FACE;                                                   \
        cell->code = CH;                                                     \
    } while (0)

#define SET_BODY(FACE, CH)                                                            \
    do {                                                                              \
        SET_IND(FACE, CH);                                                            \
                                                                                      \
        for (size_t spsi = 0; spsi < sps.len; ++spsi) {                               \
            if (sps[spsi].in_y == y + start_row && sps[spsi].in_x == x + start_col) { \
                sps[spsi].found_position = true;                                      \
                sps[spsi].out_position = iterator.position;                           \
            }                                                                         \
        }                                                                             \
    } while (0)

#define ADD_NEWLINE(FACE)               \
    do {                                \
        for (; x < window->cols; ++x) { \
            SET_BODY(FACE, ' ');        \
        }                               \
        ++y;                            \
        x = 0;                          \
        if (y == window->rows) {        \
            return;                     \
        }                               \
    } while (0)

#define ADDCH(FACE, CH)          \
    do {                         \
        SET_BODY(FACE, CH);      \
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
                                 size_t start_row,
                                 size_t start_col,
                                 cz::Slice<Screen_Position> sps) {
    ZoneScoped;

    Contents_Iterator iterator = buffer->contents.iterator_at(window->start_position);
    start_of_line(&iterator);
    if (window_cache) {
        if (buffer->changes.len() != window_cache->v.unified.change_index) {
            cache_window_unified_position(window, window_cache, iterator.position, buffer);
        }

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
    if (window->show_marks) {
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

#if 0
        if (iterator.index == 0) {
            ADDCH(A_NORMAL, '\'');
        }
#endif

        Face face = {};
        if (has_cursor) {
            apply_face(&face, editor->theme.faces[3]);
        }

        if (show_mark) {
            apply_face(&face, editor->theme.faces[4]);
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

        Face token_face;
        const size_t type_face_offset = 7;
        if (has_token && iterator.position >= token.start && iterator.position < token.end) {
            if (token.type & Token_Type::CUSTOM) {
                token_face = Token_Type_::decode(token.type);
            } else {
                token_face = editor->theme.faces[token.type + type_face_offset];
            }
        } else {
            token_face = editor->theme.faces[Token_Type::DEFAULT + type_face_offset];
        }
        apply_face(&face, token_face);

        if (face.flags & Face::INVISIBLE) {
            // Skip rendering this character as it is invisible
            continue;
        }

        char ch = iterator.get();
        if (ch == '\n') {
            SET_BODY(face, ' ');
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

    {
        // Draw cursor at end of file.
        Face face = {};
        apply_face(&face, editor->theme.faces[3]);
        for (size_t c = 0; c < cursors.len; ++c) {
            if (cursors[c].point == buffer->contents.len) {
                SET_BODY(face, ' ');
                ++x;
                break;
            }
        }
    }

    for (; y < window->rows; ++y) {
        for (; x < window->cols; ++x) {
            SET_BODY({}, ' ');
        }
        x = 0;
    }
}

static void draw_buffer_decoration(Cell* cells,
                                   size_t total_cols,
                                   Editor* editor,
                                   Window_Unified* window,
                                   Buffer* buffer,
                                   bool is_selected_window,
                                   size_t start_row,
                                   size_t start_col) {
    ZoneScoped;

    size_t y = window->rows;
    size_t x = 0;

    Face face = {};
    apply_face(&face, editor->theme.faces[0]);
    if (!buffer->is_unchanged()) {
        apply_face(&face, editor->theme.faces[1]);
    }
    if (is_selected_window) {
        apply_face(&face, editor->theme.faces[2]);
    }

    cz::AllocatedString string = {};
    string.allocator = cz::heap_allocator();
    CZ_DEFER(string.drop());
    string.reserve(1024);
    buffer->render_name(string.allocator, &string);

    for (size_t i = 0; i < editor->theme.decorations.len; ++i) {
        string.reserve(5);
        string.push(' ');
        editor->theme.decorations[i].append(buffer, window, &string);
    }

    size_t max = cz::min<size_t>(string.len(), window->cols - x);
    for (size_t i = 0; i < max; ++i) {
        SET_IND(face, string[i]);
        ++x;
    }
    for (; x < window->cols; ++x) {
        SET_IND(face, ' ');
    }
}

static void draw_buffer(Cell* cells,
                        Window_Cache* window_cache,
                        size_t total_cols,
                        Editor* editor,
                        Window_Unified* window,
                        bool is_selected_window,
                        size_t start_row,
                        size_t start_col,
                        cz::Slice<Screen_Position> sps) {
    ZoneScoped;

    for (size_t spsi = 0; spsi < sps.len; ++spsi) {
        if (start_col <= sps[spsi].in_x && sps[spsi].in_x < start_col + window->cols &&
            start_row <= sps[spsi].in_y && sps[spsi].in_y < start_row + window->rows) {
            sps[spsi].found_window = true;
            sps[spsi].out_window = window;
        }
    }

    WITH_WINDOW_BUFFER(window);
    draw_buffer_contents(cells, window_cache, total_cols, editor, buffer, window, start_row,
                         start_col, sps);
    draw_buffer_decoration(cells, total_cols, editor, window, buffer, is_selected_window, start_row,
                           start_col);
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
                        size_t count_cols,
                        cz::Slice<Screen_Position> sps) {
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
                    start_row, start_col, sps);
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
                        selected_window, start_row, start_col, count_rows, left_cols, sps);

            {
                size_t x = left_cols;
                for (size_t y = 0; y < count_rows; ++y) {
                    SET_IND({}, '|');
                }
            }

            draw_window(cells, &(*window_cache)->v.split.second, total_cols, editor, window->second,
                        selected_window, start_row, start_col + count_cols - right_cols, count_rows,
                        right_cols, sps);
        } else {
            size_t top_rows = (count_rows - 1) / 2;
            size_t bottom_rows = count_rows - top_rows - 1;

            draw_window(cells, &(*window_cache)->v.split.first, total_cols, editor, window->first,
                        selected_window, start_row, start_col, top_rows, count_cols, sps);

            {
                size_t y = top_rows;
                for (size_t x = 0; x < count_cols; ++x) {
                    SET_IND({}, '-');
                }
            }

            draw_window(cells, &(*window_cache)->v.split.second, total_cols, editor, window->second,
                        selected_window, start_row + count_rows - bottom_rows, start_col,
                        bottom_rows, count_cols, sps);
        }
        break;
    }
    }
}

bool load_mini_buffer_completion_cache(Server* server, Client* client) {
    ZoneScoped;

    if (client->mini_buffer_completion_cache.state != Completion_Cache::LOADED &&
        client->_message.tag > Message::SHOW) {
        CZ_DEBUG_ASSERT(server->editor.theme.completion_filter != nullptr);
        server->editor.theme.completion_filter(
            &client->mini_buffer_completion_cache.filter_context,
            client->mini_buffer_completion_cache.engine, &server->editor,
            &client->mini_buffer_completion_cache.engine_context);
        client->mini_buffer_completion_cache.state = Completion_Cache::LOADED;
        return true;
    } else {
        return false;
    }
}

void process_buffer_external_updates(Editor* editor, Client* client, Window* window) {
    switch (window->tag) {
    case Window::UNIFIED: {
        auto w = (Window_Unified*)window;
        WITH_BUFFER(w->id);
        buffer->check_for_external_update(client);
        break;
    }

    default: {
        auto w = (Window_Split*)window;
        process_buffer_external_updates(editor, client, w->first);
        process_buffer_external_updates(editor, client, w->second);
        break;
    }
    }
}

void render_to_cells(Cell* cells,
                     Window_Cache** window_cache,
                     size_t total_rows,
                     size_t total_cols,
                     Editor* editor,
                     Client* client,
                     cz::Slice<Screen_Position> sps) {
    ZoneScoped;

    size_t mini_buffer_height = 0;

    if (client->_message.tag != Message::NONE) {
        ZoneScopedN("Draw mini buffer");

        mini_buffer_height = 1;
        size_t results_height = 0;

        Completion_Cache* completion_cache = &client->mini_buffer_completion_cache;
        if (client->_message.tag > Message::SHOW) {
            if (completion_cache->state == Completion_Cache::LOADED) {
                results_height = completion_cache->filter_context.results.len();
                if (results_height > editor->theme.max_completion_results) {
                    results_height = editor->theme.max_completion_results;
                }
                if (results_height > total_rows / 2) {
                    results_height = total_rows / 2;
                }
            }
        }

        size_t y = 0;
        size_t x = 0;
        size_t start_row = total_rows - mini_buffer_height - results_height;
        size_t start_col = 0;

        Face minibuffer_prompt_face = {};
        apply_face(&minibuffer_prompt_face, editor->theme.faces[5]);
        for (size_t i = 0; i < client->_message.text.len && i < total_cols; ++i) {
            SET_IND(minibuffer_prompt_face, client->_message.text[i]);
            ++x;
        }

        if (client->_message.tag > Message::SHOW) {
            start_col = x;
            Window_Unified* window = client->mini_buffer_window();
            WITH_WINDOW_BUFFER(window);
            window->rows = mini_buffer_height;
            window->cols = total_cols - start_col;
            draw_buffer_contents(cells, nullptr, total_cols, editor, buffer, window, start_row,
                                 start_col, {});
        } else {
            for (; x < total_cols; ++x) {
                SET_IND({}, ' ');
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
            size_t offset = completion_cache->filter_context.selected;
            if (offset >= completion_cache->filter_context.results.len() - results_height / 2) {
                offset = completion_cache->filter_context.results.len() - results_height;
            } else if (offset < results_height / 2) {
                offset = 0;
            } else {
                offset -= results_height / 2;
            }
            for (size_t r = offset; r < results_height + offset; ++r) {
                {
                    Face face = {};
                    if (r == completion_cache->filter_context.selected) {
                        apply_face(&face, editor->theme.faces[6]);
                    }

                    cz::Str result = completion_cache->filter_context.results[r];
                    for (size_t i = 0; i < total_cols && i < result.len; ++i) {
                        SET_IND(face, result[i]);
                        ++x;
                    }
                }

                for (; x < total_cols; ++x) {
                    SET_IND({}, ' ');
                }

                x = 0;
                ++y;
            }
        }

        total_rows = start_row;
    }

    draw_window(cells, window_cache, total_cols, editor, client->window,
                client->selected_normal_window, 0, 0, total_rows, total_cols, sps);
}

}
}
