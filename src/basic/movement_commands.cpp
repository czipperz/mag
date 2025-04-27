#include "movement_commands.hpp"

#include "core/command_macros.hpp"
#include "core/movement.hpp"
#include "region_movement_commands.hpp"

namespace mag {
namespace basic {

REGISTER_COMMAND(command_forward_char);
void command_forward_char(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(forward_char);
}

REGISTER_COMMAND(command_backward_char);
void command_backward_char(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(backward_char);
}

REGISTER_COMMAND(command_forward_word);
void command_forward_word(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(forward_word);
}

REGISTER_COMMAND(command_backward_word);
void command_backward_word(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(backward_word);
}

REGISTER_COMMAND(command_forward_line);
void command_forward_line(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS([&](Contents_Iterator* it) { forward_line(buffer->mode, it); });
}

REGISTER_COMMAND(command_backward_line);
void command_backward_line(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS([&](Contents_Iterator* it) { backward_line(buffer->mode, it); });
}

static uint64_t cursor_goal_column = 0;

REGISTER_COMMAND(command_forward_line_single_cursor_visual);
void command_forward_line_single_cursor_visual(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    if (window->cursors.len == 1 && !window->show_marks) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);
        if (source.previous_command.function != command_forward_line_single_cursor_visual &&
            source.previous_command.function !=
                region_movement::command_forward_line_single_cursor_visual &&
            source.previous_command.function != command_backward_line_single_cursor_visual &&
            source.previous_command.function !=
                region_movement::command_backward_line_single_cursor_visual) {
            cursor_goal_column = get_visual_column(buffer->mode, it);
        }

        if (buffer->mode.wrap_long_lines) {
            uint64_t column = get_visual_column(buffer->mode, it);
            size_t cols = window->total_cols - line_number_cols(editor->theme, window, buffer);
            uint64_t new_goal = cursor_goal_column + cols;

            // If we have a long line and are wrapping then go to the next column down.
            while (1) {
                if (it.at_eob()) {
                eol:
                    if (column >= new_goal - (new_goal % cols)) {
                        cursor_goal_column = new_goal;
                        goto finish;
                    }
                    break;
                }

                char ch = it.get();
                if (ch == '\n')
                    goto eol;

                uint64_t column2 = char_visual_columns(buffer->mode, ch, column);
                if (column2 > new_goal) {
                    cursor_goal_column = new_goal;
                    goto finish;
                }
                column = column2;
                it.advance();
            }

            cursor_goal_column %= cols;
            goto no_wrap;
        } else {
        no_wrap:
            end_of_line(&it);
            if (it.at_eob())
                return;
            it.advance();
            go_to_visual_column(buffer->mode, &it, cursor_goal_column);
        }

    finish:
        window->cursors[0].point = it.position;
    } else {
        TRANSFORM_POINTS([&](Contents_Iterator* it) { forward_line(buffer->mode, it); });
    }
}

REGISTER_COMMAND(command_backward_line_single_cursor_visual);
void command_backward_line_single_cursor_visual(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    if (window->cursors.len == 1 && !window->show_marks) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);
        if (source.previous_command.function != command_forward_line_single_cursor_visual &&
            source.previous_command.function !=
                region_movement::command_forward_line_single_cursor_visual &&
            source.previous_command.function != command_backward_line_single_cursor_visual &&
            source.previous_command.function !=
                region_movement::command_backward_line_single_cursor_visual) {
            cursor_goal_column = get_visual_column(buffer->mode, it);
        }

        if (buffer->mode.wrap_long_lines) {
            size_t cols = window->total_cols - line_number_cols(editor->theme, window, buffer);
            if (cursor_goal_column < cols) {
                // Go to last visual line of previous line.
                start_of_line(&it);
                if (it.at_bob())
                    return;
                it.retreat();

                uint64_t column = get_visual_column(buffer->mode, it);
                column -= (column % cols);
                column += (cursor_goal_column % cols);
                cursor_goal_column = column;
            } else {
                // Go to previous visual line inside this line.
                cursor_goal_column -= cols;
            }
        } else {
            // Go to previous line.
            start_of_line(&it);
            if (it.at_bob())
                return;
            it.retreat();
        }

        go_to_visual_column(buffer->mode, &it, cursor_goal_column);
        window->cursors[0].point = it.position;
    } else {
        TRANSFORM_POINTS([&](Contents_Iterator* it) { backward_line(buffer->mode, it); });
    }
}

REGISTER_COMMAND(command_forward_paragraph);
void command_forward_paragraph(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(forward_paragraph);
}

REGISTER_COMMAND(command_backward_paragraph);
void command_backward_paragraph(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(backward_paragraph);
}

REGISTER_COMMAND(command_end_of_buffer);
void command_end_of_buffer(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    kill_extra_cursors(window, source.client);
    window->clear_show_marks_temporarily();
    if (window->cursors[0].point != 0 && window->cursors[0].point != buffer->contents.len) {
        push_jump(window, source.client, buffer);
    }
    window->cursors[0].point = buffer->contents.len;
}

REGISTER_COMMAND(command_start_of_buffer);
void command_start_of_buffer(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    kill_extra_cursors(window, source.client);
    window->clear_show_marks_temporarily();
    if (window->cursors[0].point != 0 && window->cursors[0].point != buffer->contents.len) {
        push_jump(window, source.client, buffer);
    }
    window->cursors[0].point = 0;
}

REGISTER_COMMAND(command_end_of_line);
void command_end_of_line(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(end_of_line);
}

REGISTER_COMMAND(command_start_of_line);
void command_start_of_line(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(start_of_line);
}

REGISTER_COMMAND(command_start_of_line_text);
void command_start_of_line_text(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(start_of_line_text);
}

REGISTER_COMMAND(command_end_of_line_text);
void command_end_of_line_text(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(end_of_line_text);
}

}
}
