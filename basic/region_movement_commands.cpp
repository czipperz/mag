#include "region_movement_commands.hpp"

#include "client.hpp"
#include "command_macros.hpp"
#include "commands.hpp"
#include "movement.hpp"

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
        window->show_marks = 2;
    }
}

void command_forward_char(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_forward_char(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
void command_backward_char(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_backward_char(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

void command_forward_word(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_forward_word(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
void command_backward_word(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_backward_word(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

void command_forward_paragraph(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_forward_paragraph(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
void command_backward_paragraph(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_backward_paragraph(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

void command_forward_line(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_forward_line(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
void command_backward_line(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_backward_line(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

void command_start_of_line(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_start_of_line(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
void command_end_of_line(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_end_of_line(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

void command_start_of_line_text(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_start_of_line_text(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
void command_end_of_line_text(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_end_of_line_text(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

void command_start_of_buffer(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_start_of_buffer(editor, source);
    show_marks_temporarily(source.client->selected_window());
}
void command_end_of_buffer(Editor* editor, Command_Source source) {
    set_marks(source.client->selected_window());
    basic::command_end_of_buffer(editor, source);
    show_marks_temporarily(source.client->selected_window());
}

}
}
}
