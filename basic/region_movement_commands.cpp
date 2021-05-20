#include "region_movement_commands.hpp"

#include "client.hpp"
#include "commands.hpp"

namespace mag {
namespace basic {
namespace region_movement {

static void set_mark(Client* client) {
    Window_Unified* window = client->selected_window();
    if (!window->show_marks) {
        cz::Slice<Cursor> cursors = window->cursors;
        for (size_t c = 0; c < cursors.len; ++c) {
            cursors[c].mark = cursors[c].point;
        }

        window->show_marks = true;
    }
}

void command_forward_char(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_forward_char(editor, source);
}
void command_backward_char(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_backward_char(editor, source);
}

void command_forward_word(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_forward_word(editor, source);
}
void command_backward_word(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_backward_word(editor, source);
}

void command_forward_paragraph(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_forward_paragraph(editor, source);
}
void command_backward_paragraph(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_backward_paragraph(editor, source);
}

void command_forward_line(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_forward_line(editor, source);
}
void command_backward_line(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_backward_line(editor, source);
}

void command_start_of_line(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_start_of_line(editor, source);
}
void command_end_of_line(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_end_of_line(editor, source);
}

void command_start_of_line_text(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_start_of_line_text(editor, source);
}
void command_end_of_line_text(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_end_of_line_text(editor, source);
}

void command_start_of_buffer(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_start_of_buffer(editor, source);
}
void command_end_of_buffer(Editor* editor, Command_Source source) {
    set_mark(source.client);
    basic::command_end_of_buffer(editor, source);
}

}
}
}
