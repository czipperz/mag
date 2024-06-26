#include "visible_region_commands.hpp"

#include "command_macros.hpp"
#include "movement.hpp"
#include "visible_region.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

void center_selected_cursor(Editor* editor, Window_Unified* window, const Buffer* buffer) {
    center_in_window(window, buffer->mode, editor->theme,
                     buffer->contents.iterator_at(window->cursors[window->selected_cursor].point));
}

REGISTER_COMMAND(command_center_in_window);
void command_center_in_window(Editor* editor, Command_Source source) {
    // If we have a one line mini buffer then we want to center the normal window 100% of the time.
    Window_Unified* window = source.client->selected_normal_window;
    if (source.client->_select_mini_buffer && source.client->_mini_buffer->rows() > 1) {
        window = source.client->_mini_buffer;
    }

    WITH_CONST_WINDOW_BUFFER(window);
    center_selected_cursor(editor, window, buffer);
}

REGISTER_COMMAND(command_goto_center_of_window);
void command_goto_center_of_window(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    kill_extra_cursors(window, source.client);
    window->cursors[window->selected_cursor].point =
        center_of_window(window, buffer->mode, editor->theme, &buffer->contents).position;
}

REGISTER_COMMAND(command_goto_top_of_window);
void command_goto_top_of_window(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    kill_extra_cursors(window, source.client);
    window->cursors[window->selected_cursor].point =
        top_of_window(window, buffer->mode, editor->theme, &buffer->contents).position;
}

REGISTER_COMMAND(command_goto_bottom_of_window);
void command_goto_bottom_of_window(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    kill_extra_cursors(window, source.client);
    window->cursors[window->selected_cursor].point =
        bottom_of_window(window, buffer->mode, editor->theme, &buffer->contents).position;
}

static size_t subtract_bounded(size_t left, size_t right) {
    if (left < right) {
        return left;
    } else {
        return left - right;
    }
}

REGISTER_COMMAND(command_up_page);
void command_up_page(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    if (window->cursors.len > 1) {
        if (window->selected_cursor == 0) {
            window->selected_cursor = window->cursors.len;
        }
        --window->selected_cursor;

        center_selected_cursor(editor, window, buffer);
        return;
    }

    size_t scroll_outside =
        get_scroll_outside(window->rows(), editor->theme.scroll_outside_visual_rows);

    Contents_Iterator it = buffer->contents.iterator_at(window->start_position);
    backward_visual_line(window, buffer->mode, editor->theme, &it, window->rows() - scroll_outside);
    window->start_position = it.position;

    // Go to the start of 1 row from the end of the visible region.
    forward_visual_line(window, buffer->mode, editor->theme, &it,
                        subtract_bounded(window->rows(), scroll_outside + 1));

    window->cursors[0].point = it.position;
}

REGISTER_COMMAND(command_down_page);
void command_down_page(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    if (window->cursors.len > 1) {
        ++window->selected_cursor;
        if (window->selected_cursor == window->cursors.len) {
            window->selected_cursor = 0;
        }

        center_selected_cursor(editor, window, buffer);
        return;
    }

    size_t scroll_outside =
        get_scroll_outside(window->rows(), editor->theme.scroll_outside_visual_rows);

    Contents_Iterator it = buffer->contents.iterator_at(window->start_position);
    forward_visual_line(window, buffer->mode, editor->theme, &it, window->rows() - scroll_outside);
    window->start_position = it.position;

    // We move forward one line to prevent the start position from being overridden
    // in the rendering process.  But if we're at the start of the buffer then
    // going forward one line because looks weird and won't be overridden anyway.
    if (!it.at_bob()) {
        forward_visual_line(window, buffer->mode, editor->theme, &it, scroll_outside);
    }

    window->cursors[0].point = it.position;
}

static void scroll_down(Editor* editor, Command_Source source, size_t num) {
    Window_Unified* window = source.client->mouse.window;
    if (!window)
        window = source.client->selected_window();
    WITH_CONST_WINDOW_BUFFER(window);

    Contents_Iterator it = buffer->contents.iterator_at(window->start_position);
    forward_visual_line(window, buffer->mode, editor->theme, &it, num);
    window->start_position = it.position;

    size_t scroll_outside =
        get_scroll_outside(window->rows(), editor->theme.scroll_outside_visual_rows);
    forward_visual_line(window, buffer->mode, editor->theme, &it, scroll_outside);
    if (window->cursors[window->selected_cursor].point < it.position) {
        kill_extra_cursors(window, source.client);
        window->cursors[0].point = it.position;
    }
}

static void scroll_up(Editor* editor, Command_Source source, size_t num) {
    Window_Unified* window = source.client->mouse.window;
    if (!window)
        window = source.client->selected_window();
    WITH_CONST_WINDOW_BUFFER(window);

    Contents_Iterator it = buffer->contents.iterator_at(window->start_position);
    backward_visual_line(window, buffer->mode, editor->theme, &it, num);
    window->start_position = it.position;

    size_t scroll_outside =
        get_scroll_outside(window->rows(), editor->theme.scroll_outside_visual_rows);
    forward_visual_line(window, buffer->mode, editor->theme, &it,
                        window->rows() - scroll_outside - 1);
    uint64_t start_of_last_line = it.position;
    end_of_visual_line(window, buffer->mode, editor->theme, &it);
    if (window->cursors[window->selected_cursor].point > it.position) {
        kill_extra_cursors(window, source.client);
        window->cursors[0].point = start_of_last_line;
    }
}

REGISTER_COMMAND(command_scroll_down);
void command_scroll_down(Editor* editor, Command_Source source) {
    scroll_down(editor, source, editor->theme.mouse_scroll_rows);
}
REGISTER_COMMAND(command_scroll_up);
void command_scroll_up(Editor* editor, Command_Source source) {
    scroll_up(editor, source, editor->theme.mouse_scroll_rows);
}

REGISTER_COMMAND(command_scroll_down_one);
void command_scroll_down_one(Editor* editor, Command_Source source) {
    scroll_down(editor, source, 1);
}
REGISTER_COMMAND(command_scroll_up_one);
void command_scroll_up_one(Editor* editor, Command_Source source) {
    scroll_up(editor, source, 1);
}

static void scroll_left(Editor* editor, Command_Source source, size_t num) {
    Window_Unified* window = source.client->mouse.window;
    if (!window)
        window = source.client->selected_window();
    WITH_CONST_WINDOW_BUFFER(window);

    if (window->column_offset < num) {
        window->column_offset = 0;
    } else {
        window->column_offset -= num;
    }

    uint64_t starting_position = window->cursors[window->selected_cursor].point;
    Contents_Iterator it = buffer->contents.iterator_at(starting_position);
    uint64_t column = get_visual_column(buffer->mode, it);
    uint64_t scroll_outside = editor->theme.scroll_outside_visual_columns;
    size_t cols = window->total_cols - line_number_cols(editor->theme, window, buffer);

    // If we will have to scroll right to fit the cursor then move the cursor left.
    if (column + 2 + scroll_outside > window->column_offset + cols) {
        column = 0;

        start_of_line(&it);
        while (!it.at_eob()) {
            char ch = it.get();
            if (ch == '\n') {
                CZ_PANIC("unimplemented");
            }

            column = char_visual_columns(buffer->mode, ch, column);
            it.advance();
            if (column + 2 + scroll_outside > window->column_offset + cols) {
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
    Window_Unified* window = source.client->mouse.window;
    if (!window)
        window = source.client->selected_window();
    WITH_CONST_WINDOW_BUFFER(window);

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
                break;
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

REGISTER_COMMAND(command_scroll_left);
void command_scroll_left(Editor* editor, Command_Source source) {
    scroll_left(editor, source, editor->theme.mouse_scroll_cols);
}
REGISTER_COMMAND(command_scroll_right);
void command_scroll_right(Editor* editor, Command_Source source) {
    scroll_right(editor, source, editor->theme.mouse_scroll_cols);
}

}
}
