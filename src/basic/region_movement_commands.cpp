#include "region_movement_commands.hpp"

#include "core/client.hpp"
#include "core/command_macros.hpp"
#include "core/movement.hpp"
#include "movement_commands.hpp"
#include "token_movement_commands.hpp"

namespace mag {
namespace basic {
namespace region_movement {

static void set_marks(Window_Unified* window) {
    if (!window->show_marks) {
        cz::Slice<Cursor> cursors = window->cursors;
        for (size_t c = 0; c < cursors.len; ++c) {
            cursors[c].mark = cursors[c].point;
        }
    }
}

static void show_marks_temporarily(Window_Unified* window) {
    if (!window->show_marks) {
        // If all the regions are empty then don't do anything.
        cz::Slice<Cursor> cursors = window->cursors;
        size_t c;
        for (c = 0; c < cursors.len; ++c) {
            if (cursors[c].mark != cursors[c].point) {
                break;
            }
        }

        if (c < cursors.len) {
            window->show_marks = 2;
        }
    }
}

REGISTER_COMMAND(command_forward_char);
void command_forward_char(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_forward_char(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
REGISTER_COMMAND(command_backward_char);
void command_backward_char(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_backward_char(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

REGISTER_COMMAND(command_forward_word);
void command_forward_word(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_forward_word(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
REGISTER_COMMAND(command_backward_word);
void command_backward_word(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_backward_word(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

REGISTER_COMMAND(command_forward_paragraph);
void command_forward_paragraph(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_forward_paragraph(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
REGISTER_COMMAND(command_backward_paragraph);
void command_backward_paragraph(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_backward_paragraph(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

REGISTER_COMMAND(command_forward_line);
void command_forward_line(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_forward_line(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
REGISTER_COMMAND(command_backward_line);
void command_backward_line(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_backward_line(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

REGISTER_COMMAND(command_forward_line_single_cursor_visual);
void command_forward_line_single_cursor_visual(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_forward_line_single_cursor_visual(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
REGISTER_COMMAND(command_backward_line_single_cursor_visual);
void command_backward_line_single_cursor_visual(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_backward_line_single_cursor_visual(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

REGISTER_COMMAND(command_start_of_line);
void command_start_of_line(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_start_of_line(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
REGISTER_COMMAND(command_end_of_line);
void command_end_of_line(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_end_of_line(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

REGISTER_COMMAND(command_start_of_line_text);
void command_start_of_line_text(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_start_of_line_text(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
REGISTER_COMMAND(command_end_of_line_text);
void command_end_of_line_text(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_end_of_line_text(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

REGISTER_COMMAND(command_start_of_buffer);
void command_start_of_buffer(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_start_of_buffer(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
REGISTER_COMMAND(command_end_of_buffer);
void command_end_of_buffer(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_end_of_buffer(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

REGISTER_COMMAND(command_forward_token_pair);
void command_forward_token_pair(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_forward_token_pair(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
REGISTER_COMMAND(command_backward_token_pair);
void command_backward_token_pair(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_backward_token_pair(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

REGISTER_COMMAND(command_forward_up_token_pair);
void command_forward_up_token_pair(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_forward_up_token_pair(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
REGISTER_COMMAND(command_backward_up_token_pair);
void command_backward_up_token_pair(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_backward_up_token_pair(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

REGISTER_COMMAND(command_forward_up_token_pair_or_indent);
void command_forward_up_token_pair_or_indent(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_forward_up_token_pair_or_indent(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
REGISTER_COMMAND(command_backward_up_token_pair_or_indent);
void command_backward_up_token_pair_or_indent(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_backward_up_token_pair_or_indent(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

}
}
}
