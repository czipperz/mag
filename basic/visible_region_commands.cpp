#include "visible_region_commands.hpp"

#include "command_macros.hpp"
#include "movement.hpp"
#include "visible_region.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

void center_selected_cursor(Window_Unified* window, const Buffer* buffer) {
    center_in_window(window, buffer->mode,
                     buffer->contents.iterator_at(window->cursors[window->selected_cursor].point));
}

void command_center_in_window(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    center_selected_cursor(window, buffer);
}

void command_goto_center_of_window(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->cursors[window->selected_cursor].point =
        center_of_window(window, buffer->mode, &buffer->contents).position;
}

static size_t subtract_bounded(size_t left, size_t right) {
    if (left < right) {
        return left;
    } else {
        return left - right;
    }
}

void command_up_page(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    if (window->cursors.len() > 1) {
        if (window->selected_cursor == 0) {
            window->selected_cursor = window->cursors.len();
        }
        --window->selected_cursor;

        center_selected_cursor(window, buffer);
        return;
    }

    size_t scroll_outside =
        get_scroll_outside(window->rows(), editor->theme.scroll_outside_visual_rows);

    Contents_Iterator it = buffer->contents.iterator_at(window->start_position);
    backward_visual_line(window, buffer->mode, &it, window->rows() - scroll_outside);
    window->start_position = it.position;

    // Go to the start of 1 row from the end of the visible region.
    forward_visual_line(window, buffer->mode, &it,
                        subtract_bounded(window->rows(), scroll_outside + 1));

    window->cursors[0].point = it.position;
}

void command_down_page(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    if (window->cursors.len() > 1) {
        ++window->selected_cursor;
        if (window->selected_cursor == window->cursors.len()) {
            window->selected_cursor = 0;
        }

        center_selected_cursor(window, buffer);
        return;
    }

    size_t scroll_outside =
        get_scroll_outside(window->rows(), editor->theme.scroll_outside_visual_rows);

    Contents_Iterator it = buffer->contents.iterator_at(window->start_position);
    forward_visual_line(window, buffer->mode, &it, window->rows() - scroll_outside);
    window->start_position = it.position;

    // We move forward one line to prevent the start position from being overridden
    // in the rendering process.  But if we're at the start of the buffer then
    // going forward one line because looks weird and won't be overridden anyway.
    if (!it.at_bob()) {
        forward_visual_line(window, buffer->mode, &it, scroll_outside);
    }

    window->cursors[0].point = it.position;
}

static void scroll_down(Editor* editor, Command_Source source, size_t num) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.iterator_at(window->start_position);
    forward_visual_line(window, buffer->mode, &it, num);
    window->start_position = it.position;

    size_t scroll_outside =
        get_scroll_outside(window->rows(), editor->theme.scroll_outside_visual_rows);
    forward_visual_line(window, buffer->mode, &it, scroll_outside);
    if (window->cursors[0].point < it.position) {
        kill_extra_cursors(window, source.client);
        window->cursors[0].point = it.position;
    }
}

static void scroll_up(Editor* editor, Command_Source source, size_t num) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.iterator_at(window->start_position);
    backward_visual_line(window, buffer->mode, &it, num);
    window->start_position = it.position;

    size_t scroll_outside =
        get_scroll_outside(window->rows(), editor->theme.scroll_outside_visual_rows);
    forward_visual_line(window, buffer->mode, &it, window->rows() - scroll_outside - 1);
    if (window->cursors[0].point > it.position) {
        kill_extra_cursors(window, source.client);
        window->cursors[0].point = it.position;
    }
}

void command_scroll_down(Editor* editor, Command_Source source) {
    scroll_down(editor, source, editor->theme.mouse_scroll_rows);
}
void command_scroll_up(Editor* editor, Command_Source source) {
    scroll_up(editor, source, editor->theme.mouse_scroll_rows);
}

void command_scroll_down_one(Editor* editor, Command_Source source) {
    scroll_down(editor, source, 1);
}
void command_scroll_up_one(Editor* editor, Command_Source source) {
    scroll_up(editor, source, 1);
}

static void scroll_left(Editor* editor, Command_Source source, size_t num) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    if (window->column_offset < num) {
        window->column_offset = 0;
    } else {
        window->column_offset -= num;
    }

    uint64_t starting_position = window->cursors[window->selected_cursor].point;
    Contents_Iterator it = buffer->contents.iterator_at(starting_position);
    uint64_t column = get_visual_column(buffer->mode, it);
    uint64_t scroll_outside = editor->theme.scroll_outside_visual_columns;

    // If we will have to scroll right to fit the cursor then move the cursor left.
    if (column + 2 + scroll_outside > window->column_offset + window->cols()) {
        column = 0;

        start_of_line(&it);
        while (!it.at_eob()) {
            char ch = it.get();
            if (ch == '\n') {
                CZ_PANIC("unimplemented");
            }

            column = char_visual_columns(buffer->mode, ch, column);
            it.advance();
            if (column + 2 + scroll_outside > window->column_offset + window->cols()) {
                break;
            }
        }

        // Edge case with short buffer.
        if (it.position == starting_position) {
            return;
        }

        kill_extra_cursors(window, source.client);
        window->cursors[0].point = it.position;
    }
}

static void scroll_right(Editor* editor, Command_Source source, size_t num) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    // Scroll right.
    window->column_offset += num;

    uint64_t starting_position = window->cursors[window->selected_cursor].point;
    Contents_Iterator it = buffer->contents.iterator_at(starting_position);
    uint64_t column = get_visual_column(buffer->mode, it);
    uint64_t scroll_outside = editor->theme.scroll_outside_visual_columns;

    // If we will have to scroll left to fit the cursor then move the cursor right.
    if (column < window->column_offset + scroll_outside) {
        while (!it.at_eob()) {
            char ch = it.get();
            if (ch == '\n') {
                // Scroll left such that cursor is on screen.
                if (column < scroll_outside) {
                    window->column_offset = 0;
                } else {
                    window->column_offset = column - scroll_outside;
                }
                return;
            }

            column = char_visual_columns(buffer->mode, ch, column);
            it.advance();
            if (column >= window->column_offset + scroll_outside) {
                break;
            }
        }

        // Edge case with short buffer.
        if (it.position == starting_position) {
            return;
        }

        kill_extra_cursors(window, source.client);
        window->cursors[0].point = it.position;
    }
}

void command_scroll_left(Editor* editor, Command_Source source) {
    scroll_left(editor, source, editor->theme.mouse_scroll_cols);
}
void command_scroll_right(Editor* editor, Command_Source source) {
    scroll_right(editor, source, editor->theme.mouse_scroll_cols);
}

}
}
