#include "render.hpp"

#include <math.h>
#include <stdio.h>
#include <cz/char_type.hpp>
#include <cz/format.hpp>
#include <cz/sort.hpp>
#include <cz/working_directory.hpp>
#include <tracy/Tracy.hpp>
#include "core/command_macros.hpp"
#include "core/decoration.hpp"
#include "core/diff.hpp"
#include "core/file.hpp"
#include "core/movement.hpp"
#include "core/overlay.hpp"
#include "core/server.hpp"
#include "core/token.hpp"
#include "core/tracy_format.hpp"
#include "core/visible_region.hpp"
#include "version_control/version_control.hpp"

namespace mag {
namespace render {

static bool load_completion_cache(Editor* editor,
                                  Completion_Cache* completion_cache,
                                  Completion_Filter completion_filter);

#define SET_IND(FACE, CH)                                                    \
    do {                                                                     \
        Cell* cell = &cells[(y + start_row) * total_cols + (x + start_col)]; \
        cell->face = FACE;                                                   \
        cell->code = CH;                                                     \
    } while (0)

#define SET_BODY(FACE, CH)                       \
    do {                                         \
        CZ_DEBUG_ASSERT(y < window->rows());     \
        CZ_DEBUG_ASSERT(x < window->total_cols); \
                                                 \
        SET_IND(FACE, CH);                       \
    } while (0)

#define ADD_NEWLINE(FACE)                     \
    do {                                      \
        for (; x < window->total_cols; ++x) { \
            SET_BODY(FACE, ' ');              \
        }                                     \
        ++y;                                  \
        x = 0;                                \
        if (y == window->rows()) {            \
            return;                           \
        }                                     \
    } while (0)

#define ADDCH(FACE, CH)                                                                       \
    do {                                                                                      \
        /* If we are between the start and end column then render. */                         \
        if (buffer->mode.wrap_long_lines ||                                                   \
            (column >= window->column_offset &&                                               \
             column < window->column_offset + window->total_cols -                            \
                          draw_line_numbers * line_number_buffer.cap)) {                      \
            SET_BODY(FACE, CH);                                                               \
            ++x;                                                                              \
        }                                                                                     \
        ++column;                                                                             \
                                                                                              \
        if (buffer->mode.wrap_long_lines && x == window->total_cols) {                        \
            ADD_NEWLINE({});                                                                  \
                                                                                              \
            if (draw_line_numbers) {                                                          \
                Face face = editor->theme.special_faces[Face_Type::LINE_NUMBER_LEFT_PADDING]; \
                for (size_t i = 0; i < line_number_buffer.cap - 1; ++i) {                     \
                    SET_BODY(face, ' ');                                                      \
                    ++x;                                                                      \
                }                                                                             \
                                                                                              \
                face = editor->theme.special_faces[Face_Type::LINE_NUMBER_RIGHT_PADDING];     \
                SET_BODY(face, ' ');                                                          \
                ++x;                                                                          \
            }                                                                                 \
        }                                                                                     \
    } while (0)

static void apply_face(Face* face, Face layer) {
    face->flags |= (layer.flags & ~Face::Flags::REVERSE);
    face->flags ^= (layer.flags & Face::Flags::REVERSE);
    if (layer.foreground != -1 && face->foreground == -1) {
        face->foreground = layer.foreground;
    }
    if (layer.background != -1 && face->background == -1) {
        face->background = layer.background;
    }
}

static bool try_to_make_visible(Window_Unified* window,
                                Window_Cache* window_cache,
                                const Buffer* buffer,
                                const Theme& theme,
                                Contents_Iterator* iterator,
                                size_t scroll_outside,
                                uint64_t must_be_on_screen,
                                uint64_t goal) {
    // Get the start of the visible region if we put the
    // cursor within scroll_outside lines of the end.
    Contents_Iterator start_iterator = buffer->contents.iterator_at(goal);
    backward_visual_line(window, buffer->mode, theme, &start_iterator,
                         window->rows() - scroll_outside - 1);
    start_of_visual_line(window, buffer->mode, theme, &start_iterator);

    // But the cursor must be within scroll_outside of the new top.
    Contents_Iterator test_iterator = start_iterator;
    forward_visual_line(window, buffer->mode, theme, &test_iterator, scroll_outside);

    // The cursor must be on the screen and the screen must be moving down for us to reposition.
    // If the screen would move up then we already would've fit the cursor to the screen.
    if (must_be_on_screen >= test_iterator.position &&
        start_iterator.position > iterator->position) {
        *iterator = start_iterator;
        cache_window_unified_position(window, window_cache, iterator->position, buffer);
        return true;
    }

    return false;
}

/// If we allow animated scrolling then run the code to process it.  Otherwise we
/// just jump directly to the desired starting point (`iterator.position`).
static void animate_scrolling(bool allow_animated_scrolling,
                              bool* any_animated_scrolling,
                              Window_Unified_Cache::Animated_Scrolling* animated_scrolling,
                              const Buffer* buffer,
                              Contents_Iterator* iterator) {
    if (!allow_animated_scrolling) {
        // If the user toggles on animated scrolling we should pretend
        // we've already animated scrolling to the current position.
        *animated_scrolling = {};
        return;
    }

    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    uint64_t target_line = buffer->contents.get_line_number(iterator->position);
    if (animated_scrolling->end_time < now && animated_scrolling->end_line == target_line) {
        // Animated scrolling done.
        return;
    }

    *any_animated_scrolling = true;

    const auto get_current_line = [&] {
        double percent_elapsed_time = 1;
        if (now < animated_scrolling->end_time) {
            percent_elapsed_time =
                (double)(now - animated_scrolling->start_time).count() /
                (double)(animated_scrolling->end_time - animated_scrolling->start_time).count();
        }
        if (animated_scrolling->start_line < animated_scrolling->end_line) {
            return animated_scrolling->start_line +
                   (uint64_t)((double)(animated_scrolling->end_line -
                                       animated_scrolling->start_line) *
                              percent_elapsed_time);
        } else {
            return animated_scrolling->start_line -
                   (uint64_t)((double)(animated_scrolling->start_line -
                                       animated_scrolling->end_line) *
                              percent_elapsed_time);
        }
    };
    uint64_t current_line = get_current_line();

    if (animated_scrolling->end_time < now || animated_scrolling->end_line != target_line) {
        if (animated_scrolling->end_time < now ||
            (animated_scrolling->start_line < animated_scrolling->end_line &&
             current_line > target_line) ||
            (animated_scrolling->start_line > animated_scrolling->end_line &&
             current_line < target_line)) {
            animated_scrolling->start_time = now;
            animated_scrolling->start_line = current_line;
            animated_scrolling->start_position =
                start_of_line_position(buffer->contents, current_line).position;
        }

        uint64_t distance =
            target_line > current_line ? target_line - current_line : current_line - target_line;
        if (distance < 10) {
            distance = 0;  // Prevent jitter when typing and a buffer is split screen.
        }
        animated_scrolling->end_time =
            now + std::min(std::chrono::milliseconds(200), std::chrono::milliseconds(distance / 2));
        animated_scrolling->end_line = target_line;
        animated_scrolling->end_position = iterator->position;
        current_line = get_current_line();
    }

    *iterator = start_of_line_position(buffer->contents, current_line);
}

static Contents_Iterator update_cursors_and_run_animated_scrolling(Editor* editor,
                                                                   Client* client,
                                                                   Window_Unified* window,
                                                                   cz::Arc<Buffer_Handle> handle,
                                                                   const Buffer* buffer,
                                                                   Window_Cache* window_cache,
                                                                   bool* any_animated_scrolling) {
    ZoneScoped;

    window->update_cursors(buffer, client);

// If we're in debug mode assert that we're sorted.  In release mode we just sort the cursors.
#ifdef NDEBUG
    cz::sort(window->cursors,
             [](const Cursor* left, const Cursor* right) { return left->point < right->point; });
#else
    CZ_DEBUG_ASSERT(cz::is_sorted(window->cursors, [](const Cursor* left, const Cursor* right) {
        return left->point < right->point;
    }));
#endif

    // Try to deal with out of bounds cursors and positions.
    if (window->start_position > buffer->contents.len) {
        window->start_position = buffer->contents.len;
    }
    if (window->cursors.last().point > buffer->contents.len) {
        kill_extra_cursors(window, client);
        window->cursors[0].point = window->cursors[0].mark = buffer->contents.len;
    }

    // Check that the selected cursor is in bounds.
    CZ_DEBUG_ASSERT(window->selected_cursor < window->cursors.len);

    // Delete cursors at the same point.
    kill_cursors_at_same_point(window, client);

    // Double check that the selected cursor is in bounds.
    CZ_DEBUG_ASSERT(window->selected_cursor < window->cursors.len);

    // Lock in write mode if the token cache is out of date.
    Buffer* buffer_mut = nullptr;
    bool token_cache_was_invalidated = false;
    if (buffer->changes.len != buffer->token_cache.change_index) {
        buffer_mut = handle->increase_reading_to_writing();
        token_cache_was_invalidated = !buffer_mut->token_cache.update(buffer);
    }

    Contents_Iterator iterator = buffer->contents.iterator_at(window->start_position);
    {
        // Do `start_of_visual_line` except handle the case where
        // we are just a few columns short of the next visible line.
        uint64_t column = get_visual_column(buffer->mode, iterator);

        // If we delete more than 1/3 of the width then just go to the previous line.
        column += window->total_cols / 3;

        go_to_visual_column(buffer->mode, &iterator, column - (column % window->total_cols));
    }

    if (window_cache) {
        ZoneScopedN("update window cache");

        if (buffer->changes.len != window_cache->v.unified.change_index) {
            auto changes = buffer->changes.slice_start(window_cache->v.unified.change_index);

            position_after_changes(changes, &window_cache->v.unified.visible_start);
            auto& animated_scrolling = window_cache->v.unified.animated_scrolling;
            position_after_changes(changes, &animated_scrolling.start_position);
            animated_scrolling.start_line =
                buffer->contents.get_line_number(animated_scrolling.start_position);
            position_after_changes(changes, &animated_scrolling.end_position);
            animated_scrolling.end_line =
                buffer->contents.get_line_number(animated_scrolling.end_position);

            cache_window_unified_position(window, window_cache, iterator.position, buffer);
        }

        if (window->start_position != window_cache->v.unified.visible_start) {
            // The start position variable was updated in a
            // command so we recalculate the end position.
            window_cache->v.unified.visible_start = window->start_position;
        }

        // Before we do any changes check if the mark has changed.
        bool mark_changed = window->cursors[window->selected_cursor].mark !=
                            window_cache->v.unified.selected_cursor_mark;

        // Ensure the cursor is visible
        uint64_t selected_cursor_position = window->cursors[window->selected_cursor].point;

        // If we have a window with very few rows then we will flail up and
        // down unless we bound `scroll_outside` by the number of rows.
        size_t scroll_outside = get_scroll_outside(
            window->rows(),
            client->_mini_buffer == window ? 0 : editor->theme.scroll_outside_visual_rows);

        // Calculate the minimum cursor boundary.
        Contents_Iterator visible_start_iterator = iterator;
        forward_visual_line(window, buffer->mode, editor->theme, &visible_start_iterator,
                            scroll_outside - 1);
        end_of_visual_line(window, buffer->mode, editor->theme, &visible_start_iterator);
        forward_char(&visible_start_iterator);

        // Calculate the maximum cursor boundary.
        Contents_Iterator visible_end_iterator = iterator;
        forward_visual_line(window, buffer->mode, editor->theme, &visible_end_iterator,
                            window->rows() - (scroll_outside + 1));
        end_of_visual_line(window, buffer->mode, editor->theme, &visible_end_iterator);

        // The visible_end_iterator is at the last visible character.  So if
        // we have scrolled past the entire screen then we should scroll up
        // a little so there are still scroll_outside lines on screen.
        bool eob_special_case = selected_cursor_position == buffer->contents.len &&
                                visible_start_iterator.position == buffer->contents.len;

        // Make sure the selected cursor is shown.
        if (((selected_cursor_position < visible_start_iterator.position &&
              iterator.position != 0) ||
             eob_special_case) ||
            selected_cursor_position > visible_end_iterator.position) {
            // For the line the cursor is on.
            iterator.go_to(selected_cursor_position);

            if (selected_cursor_position > visible_end_iterator.position) {
                // Scroll down such that the cursor is in bounds.
                backward_visual_line(window, buffer->mode, editor->theme, &iterator,
                                     window->rows() - (scroll_outside + 1));
            } else {
                // Scroll up such that the cursor is in bounds.
                backward_visual_line(window, buffer->mode, editor->theme, &iterator,
                                     scroll_outside);
            }

            if (editor->theme.scroll_jump_half_page_when_outside_visible_region) {
                if (selected_cursor_position > visible_end_iterator.position) {
                    Contents_Iterator it = visible_start_iterator;
                    forward_visual_line(window, buffer->mode, editor->theme, &it,
                                        window->rows() / 2);
                    if (it.position > iterator.position) {
                        iterator = it;
                    }
                } else {
                    Contents_Iterator it = visible_start_iterator;
                    backward_visual_line(window, buffer->mode, editor->theme, &it,
                                         window->rows() / 2);
                    if (it.position < iterator.position) {
                        iterator = it;
                    }
                }
            }

            // Then go to the start of that line.
            start_of_visual_line(window, buffer->mode, editor->theme, &iterator);

            // Save the scroll.
            cache_window_unified_position(window, window_cache, iterator.position, buffer);
        }

        if (buffer->mode.wrap_long_lines) {
            // If wrapping is enabled then we don't horizontally scroll.
            window->column_offset = 0;
        } else {
            // Adjust the `column_offset` such that the cursor is in bounds.
            Contents_Iterator it = buffer->contents.iterator_at(selected_cursor_position);
            uint64_t column = get_visual_column(buffer->mode, it);
            uint64_t column_grace = editor->theme.scroll_outside_visual_columns;
            if (column + 1 + column_grace > window->column_offset + window->total_cols) {
                bool scroll = true;

                // If we haven't scrolled yet then only start scrolling
                // if the line is longer than one screen width.
                if (window->column_offset == 0) {
                    Contents_Iterator eol_it = it;
                    end_of_line(&eol_it);
                    uint64_t total_columns =
                        count_visual_columns(buffer->mode, it, eol_it.position, column);
                    // Add a column for the cursor at eol by using `>=`.
                    scroll = total_columns >= window->total_cols;
                }

                if (scroll) {
                    window->column_offset = column + 1 - window->total_cols + column_grace;
                }
            } else if (column < window->column_offset + column_grace) {
                // Scroll left.  If we are within the grace or within half a screen
                // width of the destination of the left border then just go there.
                if (column < column_grace || column < window->total_cols / 2) {
                    window->column_offset = 0;
                } else {
                    window->column_offset = column - column_grace;
                }
            }
        }

        // Try to make the mark shown only if it has changed.
        if (mark_changed && window->show_marks) {
            window_cache->v.unified.selected_cursor_mark =
                window->cursors[window->selected_cursor].mark;
            try_to_make_visible(window, window_cache, buffer, editor->theme, &iterator,
                                scroll_outside, selected_cursor_position,
                                window->cursors[window->selected_cursor].mark);
        }

        // Try to fit the newly created cursors on the screen.
        if (window->cursors.len > window_cache->v.unified.cursor_count) {
            // Don't scroll down so far that we lose sight of cursors
            // above the selected cursor that are still on the screen.
            uint64_t first_visible_cursor_position = selected_cursor_position;
            for (size_t c = window->selected_cursor; c-- > 0;) {
                uint64_t point = window->cursors[c].point;
                if (point < iterator.position) {
                    break;
                }
                first_visible_cursor_position = point;
            }

            for (size_t c = window->cursors.len; c-- > window_cache->v.unified.cursor_count;) {
                if (try_to_make_visible(window, window_cache, buffer, editor->theme, &iterator,
                                        scroll_outside, first_visible_cursor_position,
                                        window->cursors[c].point)) {
                    // Fitting the point worked.  Try to also fit the mark.
                    if (window->show_marks && window->cursors[c].mark > window->cursors[c].point) {
                        try_to_make_visible(window, window_cache, buffer, editor->theme, &iterator,
                                            scroll_outside, first_visible_cursor_position,
                                            window->cursors[c].mark);
                    }

                    // If we made this one visible then all the ones before it are automatically.
                    break;
                }
            }
        }
        window_cache->v.unified.cursor_count = window->cursors.len;

        animate_scrolling(editor->theme.allow_animated_scrolling, any_animated_scrolling,
                          &window_cache->v.unified.animated_scrolling, buffer, &iterator);
    } else {
        window->start_position = iterator.position;
    }

    // If we moved into an area that isn't covered then we need to generate check points.
    if (!buffer_mut && !buffer->token_cache.is_covered(iterator.position)) {
        buffer_mut = handle->increase_reading_to_writing();
    }

    if (buffer_mut) {
        // Note: we update the token cache at the top of this function.
        CZ_DEBUG_ASSERT(buffer->token_cache.change_index == buffer->changes.len);

        // Cover the visible region with check points.
        bool had_no_check_points = buffer->token_cache.check_points.len == 0;
        buffer_mut->token_cache.generate_check_points_until(buffer, iterator.position);

        if (token_cache_was_invalidated || had_no_check_points) {
            // Start asynchronous syntax highlighting.
            TracyFormat(message, len, 1024, "Start syntax highlighting: %.*s",
                        (int)buffer->name.len, buffer->name.buffer);
            TracyMessage(message, len);
            editor->add_asynchronous_job(job_syntax_highlight_buffer(handle.clone_downgrade()));
        }

        // Unlock writing.
        handle->reduce_writing_to_reading();
    }

    return iterator;
}

static void draw_buffer_contents(Cell* cells,
                                 Window_Cache* window_cache,
                                 size_t total_cols,
                                 Editor* editor,
                                 Client* client,
                                 const Buffer* buffer,
                                 Window_Unified* window,
                                 size_t start_row,
                                 size_t start_col,
                                 size_t* cursor_pos_y,
                                 size_t* cursor_pos_x,
                                 Contents_Iterator iterator) {
    ZoneScoped;

    size_t y = 0;
    size_t x = 0;
    size_t column = get_visual_column(buffer->mode, iterator);

    Token token = {};
    uint64_t state = 0;
    bool has_token = true;
    // Note: we run `buffer->token_cache.update(buffer)` in
    // update_cursors_and_run_animated_scrolling.
    CZ_DEBUG_ASSERT(buffer->token_cache.change_index == buffer->changes.len);
    {
        Tokenizer_Check_Point check_point;
        if (buffer->token_cache.find_check_point(iterator.position, &check_point)) {
            token.end = check_point.position;
            state = check_point.state;
        }
    }

    cz::Slice<Cursor> cursors = window->cursors;

    int mark_depth = 0;
    int selected_mark_depth = 0;
    // Initialize show_mark to number of regions at start of visible region.
    if (window->show_marks) {
        for (size_t c = 0; c < cursors.len; ++c) {
            if (iterator.position > cursors[c].start() && iterator.position < cursors[c].end()) {
                ++mark_depth;
                if (c == window->selected_cursor) {
                    ++selected_mark_depth;
                }
            }
        }
    }

    CZ_DEFER({
        for (size_t i = 0; i < editor->theme.overlays.len; ++i) {
            const Overlay* overlay = &editor->theme.overlays[i];
            overlay->end_frame();
        }
        for (size_t i = 0; i < buffer->mode.overlays.len; ++i) {
            const Overlay* overlay = &buffer->mode.overlays[i];
            overlay->end_frame();
        }
    });

    for (size_t i = 0; i < editor->theme.overlays.len; ++i) {
        const Overlay* overlay = &editor->theme.overlays[i];
        overlay->start_frame(editor, client, buffer, window, iterator);
    }
    for (size_t i = 0; i < buffer->mode.overlays.len; ++i) {
        const Overlay* overlay = &buffer->mode.overlays[i];
        overlay->start_frame(editor, client, buffer, window, iterator);
    }

    cz::String line_number_buffer = {};
    CZ_DEFER(line_number_buffer.drop(cz::heap_allocator()));
    bool draw_line_numbers = false;
    if (window != client->_mini_buffer) {
        size_t cols = line_number_cols(editor->theme, window, buffer);
        draw_line_numbers = (cols > 0);
        if (draw_line_numbers)
            line_number_buffer.reserve_exact(cz::heap_allocator(), cols);
    }

    uint64_t line_number = buffer->contents.get_line_number(iterator.position) + 1;

    // Draw line number for first line.
    if (draw_line_numbers) {
        int ret = snprintf(line_number_buffer.buffer, line_number_buffer.cap, "%*zu",
                           (int)(line_number_buffer.cap - 1), line_number);
        if (ret > 0) {
            line_number_buffer.len = ret;

            size_t i = 0;
            Face face = editor->theme.special_faces[Face_Type::LINE_NUMBER_LEFT_PADDING];
            for (; i < line_number_buffer.cap - 1; ++i) {
                char ch = line_number_buffer[i];
                if (ch != ' ') {
                    break;
                }
                SET_BODY(face, ch);
                ++x;
            }

            face = editor->theme.special_faces[Face_Type::LINE_NUMBER];
            for (; i < line_number_buffer.cap - 1; ++i) {
                char ch = line_number_buffer[i];
                SET_BODY(face, ch);
                ++x;
            }

            face = editor->theme.special_faces[Face_Type::LINE_NUMBER_RIGHT_PADDING];
            SET_BODY(face, ' ');
            ++x;
        }
    }

    Contents_Iterator token_iterator = buffer->contents.iterator_at(token.end);
    for (; !iterator.at_eob(); iterator.advance()) {
        while (has_token && iterator.position >= token.end) {
#ifndef NDEBUG
            token = INVALID_TOKEN;
#endif
            has_token = buffer->mode.next_token(&token_iterator, &token, &state);
#ifndef NDEBUG
            if (has_token) {
                token.check_valid(buffer->contents.len);
            }
#endif
        }

        bool has_selected_cursor = false;
        bool has_cursor = false;
        for (size_t c = 0; c < cursors.len; ++c) {
            if (window->show_marks) {
                if (iterator.position == cursors[c].start()) {
                    ++mark_depth;
                    if (c == window->selected_cursor) {
                        ++selected_mark_depth;
                    }
                }
                if (iterator.position == cursors[c].end()) {
                    --mark_depth;
                    if (c == window->selected_cursor) {
                        --selected_mark_depth;
                    }
                }
            }
            if (iterator.position == cursors[c].point) {
                has_cursor = true;
                if (c == window->selected_cursor) {
                    has_selected_cursor = true;
                    *cursor_pos_y = y;
                    *cursor_pos_x = x;
                }
            }
        }

        if (buffer->mode.render_bucket_boundaries && iterator.index == 0) {
            Face face = {};
            face.background = {1};
            ADDCH(face, '\'');
        }

        Face face = {};
        if (has_cursor) {
            if (has_selected_cursor) {
                apply_face(&face, editor->theme.special_faces[Face_Type::SELECTED_CURSOR]);
            } else {
                apply_face(&face, editor->theme.special_faces[Face_Type::OTHER_CURSOR]);
            }
        }

        if (mark_depth > 0) {
            if (selected_mark_depth > 0) {
                apply_face(&face, editor->theme.special_faces[Face_Type::SELECTED_REGION]);
            } else {
                apply_face(&face, editor->theme.special_faces[Face_Type::OTHER_REGION]);
            }
        }

        {
            size_t j = 0;
            for (size_t i = 0; i < editor->theme.overlays.len; ++i, ++j) {
                const Overlay* overlay = &editor->theme.overlays[i];
                Face overlay_face = overlay->get_face_and_advance(buffer, window, iterator);
                apply_face(&face, overlay_face);
            }
            for (size_t i = 0; i < buffer->mode.overlays.len; ++i, ++j) {
                const Overlay* overlay = &buffer->mode.overlays[i];
                Face overlay_face = overlay->get_face_and_advance(buffer, window, iterator);
                apply_face(&face, overlay_face);
            }
        }

        Face token_face;
        if (has_token && iterator.position >= token.start && iterator.position < token.end) {
            if (token.type & Token_Type::CUSTOM) {
                token_face = Token_Type_::decode(token.type);
            } else {
                token_face = editor->theme.token_faces[token.type];
            }
        } else {
            token_face = editor->theme.token_faces[Token_Type::DEFAULT];
        }
        apply_face(&face, token_face);

        if (face.flags & Face::INVISIBLE) {
            // Skip rendering this character as it is invisible
            continue;
        }

        char ch = iterator.get();
        if (ch == '\n') {
            if (x < window->total_cols) {
                SET_BODY(face, ' ');
            }
            ++x;
            ++column;

            // Draw newline padding with faces from overlays
            {
                Face face;
                size_t j = 0;
                for (size_t i = 0; i < editor->theme.overlays.len; ++i, ++j) {
                    const Overlay* overlay = &editor->theme.overlays[i];
                    Face overlay_face = overlay->get_face_newline_padding(buffer, window, iterator);
                    apply_face(&face, overlay_face);
                }
                for (size_t i = 0; i < buffer->mode.overlays.len; ++i, ++j) {
                    const Overlay* overlay = &buffer->mode.overlays[i];
                    Face overlay_face = overlay->get_face_newline_padding(buffer, window, iterator);
                    apply_face(&face, overlay_face);
                }
                column = 0;
                ADD_NEWLINE(face);
            }

            // Draw line number.  Note the first line number is drawn before the loop.
            if (draw_line_numbers) {
                line_number += 1;
                int ret = snprintf(line_number_buffer.buffer, line_number_buffer.cap, "%*zu",
                                   (int)(line_number_buffer.cap - 1), line_number);
                if (ret > 0) {
                    line_number_buffer.len = ret;

                    size_t i = 0;
                    Face face = editor->theme.special_faces[Face_Type::LINE_NUMBER_LEFT_PADDING];
                    for (; i < line_number_buffer.cap - 1; ++i) {
                        char ch = line_number_buffer[i];
                        if (ch != ' ') {
                            break;
                        }
                        SET_BODY(face, ch);
                        ++x;
                    }

                    face = editor->theme.special_faces[Face_Type::LINE_NUMBER];
                    for (; i < line_number_buffer.cap - 1; ++i) {
                        char ch = line_number_buffer[i];
                        SET_BODY(face, ch);
                        ++x;
                    }

                    face = editor->theme.special_faces[Face_Type::LINE_NUMBER_RIGHT_PADDING];
                    SET_BODY(face, ' ');
                    ++x;
                }
            }
        } else if (ch == '\t') {
            size_t end_column = column + buffer->mode.tab_width;
            end_column -= end_column % buffer->mode.tab_width;
            for (size_t i = column; i < end_column; ++i) {
                ADDCH(face, ' ');
            }
        } else if (cz::is_print(ch)) {
            ADDCH(face, ch);
        } else {
            ADDCH(face, '\\');
            ADDCH(face, '[');
            bool already = false;
            unsigned uch = ch;
            if ((uch / 100) % 10) {
                ADDCH(face, (uch / 100) % 10 + '0');
                already = true;
            }
            if ((uch / 10) % 10 || already) {
                ADDCH(face, (uch / 10) % 10 + '0');
            }
            ADDCH(face, uch % 10 + '0');
            ADDCH(face, ';');
        }
    }

    // Draw cursor at end of file.
    if (cursors.last().point == buffer->contents.len) {
        Face face = {};
        if (cursors.len - 1 == window->selected_cursor) {
            apply_face(&face, editor->theme.special_faces[Face_Type::SELECTED_CURSOR]);
        } else {
            apply_face(&face, editor->theme.special_faces[Face_Type::OTHER_CURSOR]);
        }

        if (x < window->total_cols) {
            SET_BODY(face, ' ');
        }
        ++x;
    }

    // Clear the rest of the window.
    for (; y < window->rows(); ++y) {
        for (; x < window->total_cols; ++x) {
            SET_BODY({}, ' ');
        }
        x = 0;
    }
}

static void draw_buffer_decoration(Cell* cells,
                                   size_t total_cols,
                                   Editor* editor,
                                   Client* client,
                                   Window_Unified* window,
                                   const Buffer* buffer,
                                   bool is_selected_window,
                                   size_t start_row,
                                   size_t start_col) {
    ZoneScoped;

    Face face = {};
    if (!buffer->is_unchanged()) {
        apply_face(&face, editor->theme.special_faces[Face_Type::UNSAVED_MODE_LINE]);
    }
    if (is_selected_window) {
        apply_face(&face, editor->theme.special_faces[Face_Type::SELECTED_MODE_LINE]);
    }
    apply_face(&face, editor->theme.special_faces[Face_Type::DEFAULT_MODE_LINE]);

    cz::Heap_String string = {};
    CZ_DEFER(string.drop());
    string.reserve(1024);
    buffer->render_name(cz::heap_allocator(), &string);
    size_t starting_len = string.len;

    cz::append(&string, ' ');

    for (size_t i = 0; i < editor->theme.decorations.len; ++i) {
        if (editor->theme.decorations[i].append(editor, client, buffer, window,
                                                cz::heap_allocator(), &string)) {
            cz::append(&string, ' ');
        }
    }
    for (size_t i = 0; i < buffer->mode.decorations.len; ++i) {
        if (buffer->mode.decorations[i].append(editor, client, buffer, window, cz::heap_allocator(),
                                               &string)) {
            cz::append(&string, ' ');
        }
    }

    // Attempt to shorten the start of the string.
    if (string.len > window->total_cols) {
        cz::Str rendered = string.slice_end(starting_len);
        if (buffer->type == Buffer::TEMPORARY) {
            // *identifier detail1 detail2 etc* => *identifier ...*
            const char* space = rendered.find(' ');
            const char* star = rendered.rfind('*');
            if (space && star && star - (space + 1) > 3) {
                string.remove_range(space + 1 - string.buffer, star - string.buffer);
                string.insert(space + 1 - string.buffer, "...");
                rendered.len -= star - (space + 1);
                rendered.len += 3;
            }
        }

        if (string.len > window->total_cols && buffer->directory.len != 0) {
            // *name* (/path1/path2/path3) => *name* (.../path3)
            cz::Str dir = rendered;
            if (buffer->type == Buffer::TEMPORARY) {
                dir = rendered.slice_start(rendered.len - buffer->directory.len - 1);
                dir.len -= 2;  // remove `/)`
            }

            // Find the shortest removable portion such that it will fit.
            const char* slash = dir.buffer;
            while (1) {
                slash = dir.slice_start(slash + 1).find('/');
                if (!slash) {
                    break;
                }
                if (string.len - (slash - dir.buffer) + 3 < window->total_cols) {
                    break;
                }
            }

            if (!slash) {
                slash = dir.rfind('/');
            }

            if (slash && (slash - dir.buffer) > 3) {
                string.remove_range(dir.buffer - string.buffer, slash - string.buffer);
                string.insert(dir.buffer - string.buffer, "...");
            }
        }
    }

    size_t y = window->rows();
    size_t x = 0;
    size_t max = cz::min<size_t>(string.len, window->total_cols);
    for (size_t i = 0; i < max; ++i) {
        SET_IND(face, string[i]);
        ++x;
    }
    for (; x < window->total_cols; ++x) {
        SET_IND(face, ' ');
    }
}

static void draw_window_completion(Cell* cells,
                                   Editor* editor,
                                   Client* client,
                                   Window_Unified* window,
                                   const Buffer* buffer,
                                   size_t total_cols,
                                   size_t start_row,
                                   size_t start_col,
                                   size_t cursor_pos_y,
                                   size_t cursor_pos_x) {
    if (!window->completing) {
        return;
    }
    window->update_completion_cache(buffer);
    if (!window->completing) {
        return;
    }

    bool engine_change = load_completion_cache(editor, &window->completion_cache,
                                               editor->theme.window_completion_filter);
    if (window->completion_cache.state == Completion_Cache::LOADED &&
        window->completion_cache.filter_context.results.len == 0) {
        client->show_message("No completion results");
        window->abort_completion();
        return;
    }

    if (window->completion_cache.state == Completion_Cache::LOADED || engine_change) {
        // todo: deduplicate with the code to draw mini buffer completion cache
        size_t height = window->completion_cache.filter_context.results.len;
        if (height > editor->theme.max_completion_results) {
            height = editor->theme.max_completion_results;
        }
        if (height > window->rows() / 2) {
            height = window->rows() / 2;
        }

        size_t offset = window->completion_cache.filter_context.selected;
        if (offset >= window->completion_cache.filter_context.results.len - height / 2) {
            offset = window->completion_cache.filter_context.results.len - height;
        } else if (offset < height / 2) {
            offset = 0;
        } else {
            offset -= height / 2;
        }

        size_t width = 0;
        for (size_t r = offset; r < height + offset; ++r) {
            if (window->completion_cache.filter_context.results[r].len > width) {
                width = window->completion_cache.filter_context.results[r].len;
            }
        }
        width += window->completion_cache.engine_context.result_prefix.len;
        width += window->completion_cache.engine_context.result_suffix.len;

        size_t y = cursor_pos_y;
        size_t x = cursor_pos_x;

        bool narrow_line = false;
        if (x + width > window->total_cols) {
            if (window->total_cols >= width) {
                x = window->total_cols - width;
            } else {
                x = 0;
                width = window->total_cols;
            }
            narrow_line = true;
        }

        size_t start_x = x;

        size_t lines_above = cursor_pos_y + !narrow_line;
        size_t lines_below = window->rows() - cursor_pos_y - narrow_line;
        if (narrow_line + height > lines_below) {
            if (lines_above > lines_below) {
                y = cursor_pos_y - height + !narrow_line;
            } else {
                height = lines_below;
            }
        } else {
            y += narrow_line;
        }

        for (size_t r = offset; r < height + offset; ++r) {
            Face face = {};
            if (r == window->completion_cache.filter_context.selected) {
                apply_face(&face,
                           editor->theme.special_faces[Face_Type::WINDOW_COMPLETION_SELECTED]);
            } else {
                apply_face(&face, editor->theme.special_faces[Face_Type::WINDOW_COMPLETION_NORMAL]);
            }

            cz::Str result = window->completion_cache.filter_context.results[r];
            for (size_t i = 0;
                 i < width && i < window->completion_cache.engine_context.result_prefix.len; ++i) {
                SET_IND(face, window->completion_cache.engine_context.result_prefix[i]);
                ++x;
            }
            for (size_t i = 0; i < width && i < result.len; ++i) {
                SET_IND(face, result[i]);
                ++x;
            }
            for (size_t i = 0;
                 i < width && i < window->completion_cache.engine_context.result_suffix.len; ++i) {
                SET_IND(face, window->completion_cache.engine_context.result_suffix[i]);
                ++x;
            }

            for (; x < start_x + width; ++x) {
                SET_IND(face, ' ');
            }

            x = start_x;
            ++y;
        }
    }
}

static void update_working_directory(cz::Str directory) {
    cz::Heap_String new_wd = {};
    CZ_DEFER(new_wd.drop());
    if (!version_control::get_root_directory(directory, cz::heap_allocator(), &new_wd))
        return;

    static cz::Heap_String previous_wd = {};
    if (previous_wd == new_wd)
        return;
    std::swap(previous_wd, new_wd);
    (void)cz::set_working_directory(previous_wd.buffer);
}

static void draw_buffer(Cell* cells,
                        Window_Cache* window_cache,
                        size_t total_cols,
                        Editor* editor,
                        Client* client,
                        bool* any_animated_scrolling,
                        Window_Unified* window,
                        const Buffer* buffer,
                        bool is_selected_window,
                        size_t start_row,
                        size_t start_col) {
    ZoneScoped;

    if (is_selected_window) {
        update_working_directory(buffer->directory);
    }

    Contents_Iterator iterator =
        update_cursors_and_run_animated_scrolling(editor, client, window, window->buffer_handle,
                                                  buffer, window_cache, any_animated_scrolling);

    size_t cursor_pos_y = 0, cursor_pos_x = 0;
    draw_buffer_contents(cells, window_cache, total_cols, editor, client, buffer, window, start_row,
                         start_col, &cursor_pos_y, &cursor_pos_x, iterator);
    draw_buffer_decoration(cells, total_cols, editor, client, window, buffer, is_selected_window,
                           start_row, start_col);
    draw_window_completion(cells, editor, client, window, buffer, total_cols, start_row, start_col,
                           cursor_pos_y, cursor_pos_x);
    client->cursor_pos_y = cursor_pos_y;
    client->cursor_pos_x = cursor_pos_x;
}

static void setup_unified_window_cache(Editor* editor,
                                       Window_Unified* window,
                                       const Buffer* buffer,
                                       Window_Cache** window_cache) {
    if (!*window_cache) {
        *window_cache = cz::heap_allocator().alloc<Window_Cache>();
        CZ_ASSERT(*window_cache);
        cache_window_unified_create(editor, *window_cache, window, buffer);
    } else if ((*window_cache)->tag != window->tag) {
        destroy_window_cache_children(*window_cache);
        cache_window_unified_create(editor, *window_cache, window, buffer);
    } else if ((*window_cache)->v.unified.window_id != window->id) {
        cache_window_unified_create(editor, *window_cache, window, buffer);
    }
}

static void draw_window(Cell* cells,
                        Window_Cache** window_cache,
                        size_t total_cols,
                        Editor* editor,
                        Client* client,
                        bool* any_animated_scrolling,
                        Window* w,
                        Window* selected_window,
                        size_t start_row,
                        size_t start_col) {
    ZoneScoped;

    switch (w->tag) {
    case Window::UNIFIED: {
        Window_Unified* window = (Window_Unified*)w;

        WITH_CONST_WINDOW_BUFFER(window, client);

        setup_unified_window_cache(editor, window, buffer, window_cache);

        draw_buffer(cells, *window_cache, total_cols, editor, client, any_animated_scrolling,
                    window, buffer, window == selected_window, start_row, start_col);
        break;
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)w;

        if (!*window_cache) {
            *window_cache = cz::heap_allocator().alloc<Window_Cache>();
            CZ_ASSERT(*window_cache);
            (*window_cache)->tag = window->tag;
            (*window_cache)->v.split = {};
        } else if ((*window_cache)->tag != window->tag) {
            destroy_window_cache_children(*window_cache);
            (*window_cache)->tag = window->tag;
            (*window_cache)->v.split = {};
        }

        if (window->tag == Window::VERTICAL_SPLIT) {
            draw_window(cells, &(*window_cache)->v.split.first, total_cols, editor, client,
                        any_animated_scrolling, window->first, selected_window, start_row,
                        start_col);

            {
                size_t x = window->first->total_cols;
                for (size_t y = 0; y < window->total_rows; ++y) {
                    SET_IND({}, '|');
                }
            }

            draw_window(cells, &(*window_cache)->v.split.second, total_cols, editor, client,
                        any_animated_scrolling, window->second, selected_window, start_row,
                        start_col + window->total_cols - window->second->total_cols);
        } else {
            draw_window(cells, &(*window_cache)->v.split.first, total_cols, editor, client,
                        any_animated_scrolling, window->first, selected_window, start_row,
                        start_col);

            // No separator as the window title acts as it.

            draw_window(cells, &(*window_cache)->v.split.second, total_cols, editor, client,
                        any_animated_scrolling, window->second, selected_window,
                        start_row + window->total_rows - window->second->total_rows, start_col);
        }
        break;
    }
    }
}

static void recalculate_mouse_recursive(Client* client,
                                        const Theme& theme,
                                        Window* w,
                                        Mouse_Position* mouse,
                                        size_t start_row,
                                        size_t start_col) {
    switch (w->tag) {
    case Window::UNIFIED: {
        Window_Unified* window = (Window_Unified*)w;
        // Test if the mouse is in bounds.
        if (mouse->has_client_position &&
            (start_col <= mouse->client_column &&
             mouse->client_column < start_col + window->total_cols) &&
            (start_row <= mouse->client_row && mouse->client_row < start_row + window->rows())) {
            // Mark this window as the mouse's window.
            WITH_CONST_WINDOW_BUFFER(window, client);
            mouse->window = window;
            mouse->window_row = (uint32_t)(mouse->client_row - start_row);
            mouse->window_column = (uint32_t)(mouse->client_column - start_col -
                                              line_number_cols(theme, window, buffer));
        }
        break;
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)w;
        recalculate_mouse_recursive(client, theme, window->first, mouse, start_row, start_col);
        if (window->tag == Window::VERTICAL_SPLIT) {
            recalculate_mouse_recursive(
                client, theme, window->second, mouse, start_row,
                start_col + window->total_cols - window->second->total_cols);
        } else {
            recalculate_mouse_recursive(client, theme, window->second, mouse,
                                        start_row + window->total_rows - window->second->total_rows,
                                        start_col);
        }
        break;
    }
    }
}

void recalculate_mouse(const Theme& theme, Client* client) {
    client->mouse.window = nullptr;
    recalculate_mouse_recursive(client, theme, client->window, &client->mouse, 0, 0);
}

bool load_mini_buffer_completion_cache(Server* server, Client* client) {
    ZoneScoped;

    if (client->_message.tag <= Message::SHOW) {
        return false;
    }

    return load_completion_cache(&server->editor, &client->mini_buffer_completion_cache,
                                 server->editor.theme.mini_buffer_completion_filter);
}

static bool load_completion_cache(Editor* editor,
                                  Completion_Cache* completion_cache,
                                  Completion_Filter completion_filter) {
    CZ_DEBUG_ASSERT(completion_filter != nullptr);

    cz::String selected_result = {};
    CZ_DEFER(selected_result.drop(cz::heap_allocator()));
    bool has_selected_result = false;
    if (completion_cache->filter_context.selected < completion_cache->filter_context.results.len) {
        cz::Str selected_result_str =
            completion_cache->filter_context.results[completion_cache->filter_context.selected];
        selected_result.reserve(cz::heap_allocator(), selected_result_str.len);
        selected_result.append(selected_result_str);
        has_selected_result = true;
    }

    bool engine_change = false;
    if (completion_cache->engine) {
        engine_change =
            completion_cache->engine(editor, &completion_cache->engine_context,
                                     completion_cache->state == Completion_Cache::INITIAL);
    }

    if (completion_cache->state != Completion_Cache::LOADED || engine_change) {
        completion_cache->filter_context.selected = 0;
        completion_cache->filter_context.results.len = 0;
        completion_cache->filter_context.results.reserve(
            completion_cache->engine_context.results.len);
        completion_filter(editor, &completion_cache->filter_context,
                          &completion_cache->engine_context, selected_result, has_selected_result);
    }

    completion_cache->state = Completion_Cache::LOADED;
    return true;
}

void process_buffer_external_updates(Client* client, Window* window) {
    ZoneScoped;

    switch (window->tag) {
    case Window::UNIFIED: {
        Window_Unified* w = (Window_Unified*)window;
        WITH_CONST_WINDOW_BUFFER(w, client);

        if (!buffer->is_unchanged() || !buffer->has_file_time) {
            return;
        }

        cz::String path = {};
        CZ_DEFER(path.drop(cz::heap_allocator()));
        if (!buffer->get_path(cz::heap_allocator(), &path)) {
            return;
        }

        cz::File_Time file_time = buffer->file_time;
        if (check_out_of_date_and_update_file_time(path.buffer, &file_time)) {
            Buffer* buffer_mut = handle->increase_reading_to_writing();
            buffer_mut->file_time = file_time;
            const char* message = reload_file(buffer_mut);
            if (message)
                client->show_message(message);
        }

        break;
    }

    default: {
        auto w = (Window_Split*)window;
        process_buffer_external_updates(client, w->first);
        process_buffer_external_updates(client, w->second);
        break;
    }
    }
}

static void draw_mini_buffer_buffer(Cell* cells,
                                    Window_Cache** mini_buffer_window_cache,
                                    size_t total_rows,
                                    size_t total_cols,
                                    Editor* editor,
                                    Client* client,
                                    bool* any_animated_scrolling,
                                    Window_Unified* window,
                                    cz::Arc<Buffer_Handle> handle,
                                    const Buffer* buffer,
                                    size_t mini_buffer_height,
                                    size_t results_height,
                                    size_t message_width) {
    size_t y = 0;
    size_t x = 0;
    size_t start_row = total_rows - mini_buffer_height - results_height;
    size_t start_col = 0;

    Face minibuffer_prompt_face = {};
    apply_face(&minibuffer_prompt_face, editor->theme.special_faces[Face_Type::MINI_BUFFER_PROMPT]);
    {
        WITH_CONST_BUFFER_HANDLE(client->messages_buffer_handle);

        Contents_Iterator it = buffer->contents.iterator_at(client->_message.start);
        // TODO: handle multi line mini buffer message
        for (; x < message_width; it.advance()) {
            SET_IND(minibuffer_prompt_face, it.get());
            ++x;
        }
    }

    // Clear the other lines below the message.
    for (y = 1; y < window->rows(); ++y) {
        // TODO: handle multi line mini buffer message
        for (x = 0; x < message_width; ++x) {
            SET_IND(minibuffer_prompt_face, ' ');
        }
    }

    if (client->_message.tag > Message::SHOW) {
        start_col = x;

        Contents_Iterator iterator = update_cursors_and_run_animated_scrolling(
            editor, client, window, handle, buffer, *mini_buffer_window_cache,
            any_animated_scrolling);
        size_t cursor_pos_y, cursor_pos_x;
        draw_buffer_contents(cells, *mini_buffer_window_cache, total_cols, editor, client, buffer,
                             window, start_row, start_col, &cursor_pos_y, &cursor_pos_x, iterator);
    } else {
        y = 0;
        for (; x < total_cols; ++x) {
            SET_IND({}, ' ');
        }

        if (std::chrono::system_clock::now() - client->_message_time > std::chrono::seconds(5)) {
            client->_message.tag = Message::NONE;
        }
    }
}

static void draw_mini_buffer_results(Cell* cells,
                                     Window_Cache** mini_buffer_window_cache,
                                     size_t total_rows,
                                     size_t total_cols,
                                     Editor* editor,
                                     Client* client,
                                     const Buffer* buffer,
                                     size_t results_height,
                                     Completion_Cache* completion_cache) {
    size_t y = 0;
    size_t x = 0;
    size_t start_row = total_rows - results_height;
    size_t start_col = 0;
    size_t offset = completion_cache->filter_context.selected;
    if (offset >= completion_cache->filter_context.results.len - results_height / 2) {
        offset = completion_cache->filter_context.results.len - results_height;
    } else if (offset < results_height / 2) {
        offset = 0;
    } else {
        offset -= results_height / 2;
    }

    Tokenizer minibuffer_next_token = buffer->mode.next_token;
    for (size_t r = offset; r < results_height + offset; ++r) {
        // Syntax highlight results with the Tokenizer set on the mini buffer.
        Contents contents = {};
        CZ_DEFER(contents.drop());

        cz::Str result = completion_cache->filter_context.results[r];
        contents.insert(0, completion_cache->engine_context.result_suffix);
        contents.insert(0, result);
        contents.insert(0, completion_cache->engine_context.result_prefix);

        // TODO: have some sort of scroll or wrap for results?
        if (contents.len > total_cols) {
            contents.remove(total_cols, contents.len - total_cols);
        }

        Contents_Iterator token_iterator = contents.start();
        uint64_t state = 0;
        Token token;
#ifndef NDEBUG
        token = INVALID_TOKEN;
#endif
        bool has_token = minibuffer_next_token(&token_iterator, &token, &state);
#ifndef NDEBUG
        if (has_token) {
            token.check_valid(contents.len);
        }
#endif

        Face base_face = {};
        if (r == completion_cache->filter_context.selected) {
            apply_face(&base_face,
                       editor->theme.special_faces[Face_Type::MINI_BUFFER_COMPLETION_SELECTED]);
        }

        for (Contents_Iterator it = contents.start(); !it.at_eob(); it.advance()) {
            Face face = base_face;

            while (has_token) {
                if (it.position < token.start) {
                    // Before the start of the token; do nothing.
                    break;
                } else if (it.position < token.end) {
                    // The token is active.
                    if (token.type & Token_Type::CUSTOM) {
                        apply_face(&face, Token_Type_::decode(token.type));
                    } else {
                        apply_face(&face, editor->theme.token_faces[token.type]);
                    }
                    break;
                } else {
                    // Go to the next token.
#ifndef NDEBUG
                    token = INVALID_TOKEN;
#endif
                    has_token = minibuffer_next_token(&token_iterator, &token, &state);
#ifndef NDEBUG
                    if (has_token) {
                        token.check_valid(contents.len);
                    }
#endif
                    continue;
                }
            }

            SET_IND(face, it.get());
            ++x;
        }

        for (; x < total_cols; ++x) {
            SET_IND({}, ' ');
        }

        x = 0;
        ++y;
    }
}

static size_t draw_mini_buffer(Cell* cells,
                               Window_Cache** window_cache,
                               Window_Cache** mini_buffer_window_cache,
                               size_t total_rows,
                               size_t total_cols,
                               Editor* editor,
                               Client* client,
                               bool* any_animated_scrolling) {
    ZoneScoped;

    size_t mini_buffer_height = 1;
    size_t results_height = 0;

    Completion_Cache* completion_cache = &client->mini_buffer_completion_cache;
    if (client->_message.tag > Message::SHOW) {
        if (completion_cache->state == Completion_Cache::LOADED) {
            results_height = completion_cache->filter_context.results.len;
            if (results_height > editor->theme.max_completion_results) {
                results_height = editor->theme.max_completion_results;
            }
            if (results_height > total_rows / 2) {
                results_height = total_rows / 2;
            }
        }
    }

    Window_Unified* window = client->mini_buffer_window();
    WITH_CONST_WINDOW_BUFFER(window, client);

    size_t message_width =
        std::min(client->_message.end - client->_message.start, (uint64_t)total_cols);
    // Add 1 for the title bar even though the mini buffer doesn't have a title bar.
    window->total_rows = 1 + mini_buffer_height;
    window->total_cols = total_cols - message_width;

    // We want to draw the mini buffer as small as possible.
    // TODO: draw message on multiple lines if it is too long.
    if (client->_message.tag > Message::SHOW) {
        Contents_Iterator it = buffer->contents.start();

        // Eat the first line.
        end_of_visual_line(window, buffer->mode, editor->theme, &it);
        forward_char(&it);

        for (; mini_buffer_height < editor->theme.mini_buffer_max_height; ++mini_buffer_height) {
            // If there is no subsequent line then stop.
            uint64_t backup = it.position;
            end_of_visual_line(window, buffer->mode, editor->theme, &it);
            forward_char(&it);
            if (it.position == backup) {
                break;
            }
        }

        window->total_rows = 1 + mini_buffer_height;

        setup_unified_window_cache(editor, window, buffer, mini_buffer_window_cache);
    }

    draw_mini_buffer_buffer(cells, mini_buffer_window_cache, total_rows, total_cols, editor, client,
                            any_animated_scrolling, window, handle, buffer, mini_buffer_height,
                            results_height, message_width);
    draw_mini_buffer_results(cells, mini_buffer_window_cache, total_rows, total_cols, editor,
                             client, buffer, results_height, completion_cache);
    return total_rows - mini_buffer_height - results_height;
}

void render_to_cells(Cell* cells,
                     Window_Cache** window_cache,
                     Window_Cache** mini_buffer_window_cache,
                     size_t total_rows,
                     size_t total_cols,
                     Editor* editor,
                     Client* client,
                     bool* any_animated_scrolling) {
    ZoneScoped;

    if (client->_message.tag != Message::NONE) {
        total_rows = draw_mini_buffer(cells, window_cache, mini_buffer_window_cache, total_rows,
                                      total_cols, editor, client, any_animated_scrolling);
    }

    client->window->set_size(total_rows, total_cols);
    draw_window(cells, window_cache, total_cols, editor, client, any_animated_scrolling,
                client->window, client->selected_normal_window, 0, 0);
    recalculate_mouse(editor->theme, client);
}

}
}
