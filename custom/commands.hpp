#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace custom {

void command_set_mark(mag::Editor* editor, mag::Command_Source source);
void command_swap_mark_point(mag::Editor* editor, mag::Command_Source source);

void command_cut(mag::Editor* editor, mag::Command_Source source);
void command_copy(mag::Editor* editor, mag::Command_Source source);
void command_paste(mag::Editor* editor, mag::Command_Source source);

void command_forward_char(mag::Editor* editor, mag::Command_Source source);
void command_backward_char(mag::Editor* editor, mag::Command_Source source);

void command_forward_word(mag::Editor* editor, mag::Command_Source source);
void command_backward_word(mag::Editor* editor, mag::Command_Source source);

void command_forward_line(mag::Editor* editor, mag::Command_Source source);
void command_backward_line(mag::Editor* editor, mag::Command_Source source);

void command_end_of_buffer(mag::Editor* editor, mag::Command_Source source);
void command_start_of_buffer(mag::Editor* editor, mag::Command_Source source);

void command_end_of_line(mag::Editor* editor, mag::Command_Source source);
void command_start_of_line(mag::Editor* editor, mag::Command_Source source);

void command_start_of_line_text(mag::Editor* editor, mag::Command_Source source);

void command_shift_line_forward(mag::Editor* editor, mag::Command_Source source);
void command_shift_line_backward(mag::Editor* editor, mag::Command_Source source);

void command_delete_backward_char(mag::Editor* editor, mag::Command_Source source);
void command_delete_forward_char(mag::Editor* editor, mag::Command_Source source);

void command_delete_backward_word(mag::Editor* editor, mag::Command_Source source);
void command_delete_forward_word(mag::Editor* editor, mag::Command_Source source);

void command_transpose_characters(mag::Editor* editor, mag::Command_Source source);

void command_open_line(mag::Editor* editor, mag::Command_Source source);
void command_insert_newline(mag::Editor* editor, mag::Command_Source source);

void command_undo(mag::Editor* editor, mag::Command_Source source);
void command_redo(mag::Editor* editor, mag::Command_Source source);

void command_stop_action(mag::Editor* editor, mag::Command_Source source);

void command_quit(mag::Editor* editor, mag::Command_Source source);

void command_create_cursor_forward_line(mag::Editor* editor, mag::Command_Source source);
void command_create_cursor_backward_line(mag::Editor* editor, mag::Command_Source source);
void command_create_cursor_forward_search(mag::Editor* editor, mag::Command_Source source);
void command_create_cursor_backward_search(mag::Editor* editor, mag::Command_Source source);
void command_create_cursor_forward(mag::Editor* editor, mag::Command_Source source);
void command_create_cursor_backward(mag::Editor* editor, mag::Command_Source source);

void command_search_forward(mag::Editor* editor, mag::Command_Source source);
void command_search_backward(mag::Editor* editor, mag::Command_Source source);

}
