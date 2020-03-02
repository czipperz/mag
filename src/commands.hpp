#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {

void command_set_mark(Editor* editor, Command_Source source);
void command_swap_mark_point(Editor* editor, Command_Source source);

void command_cut(Editor* editor, Command_Source source);
void command_copy(Editor* editor, Command_Source source);
void command_paste(Editor* editor, Command_Source source);

void command_forward_char(Editor* editor, Command_Source source);
void command_backward_char(Editor* editor, Command_Source source);

void command_forward_word(Editor* editor, Command_Source source);
void command_backward_word(Editor* editor, Command_Source source);

void command_forward_line(Editor* editor, Command_Source source);
void command_backward_line(Editor* editor, Command_Source source);

void command_end_of_buffer(Editor* editor, Command_Source source);
void command_start_of_buffer(Editor* editor, Command_Source source);

void command_end_of_line(Editor* editor, Command_Source source);
void command_start_of_line(Editor* editor, Command_Source source);

void command_start_of_line_text(Editor* editor, Command_Source source);

void command_shift_line_forward(Editor* editor, Command_Source source);
void command_shift_line_backward(Editor* editor, Command_Source source);

void command_delete_backward_char(Editor* editor, Command_Source source);
void command_delete_forward_char(Editor* editor, Command_Source source);

void command_delete_backward_word(Editor* editor, Command_Source source);
void command_delete_forward_word(Editor* editor, Command_Source source);

void command_open_line(Editor* editor, Command_Source source);
void command_insert_newline(Editor* editor, Command_Source source);

void command_undo(Editor* editor, Command_Source source);
void command_redo(Editor* editor, Command_Source source);

void command_stop_action(Editor* editor, Command_Source source);

void command_quit(Editor* editor, Command_Source source);

void command_create_cursor_forward_line(Editor* editor, Command_Source source);
void command_create_cursor_backward_line(Editor* editor, Command_Source source);
void command_create_cursor_forward_search(Editor* editor, Command_Source source);
void command_create_cursor_backward_search(Editor* editor, Command_Source source);
void command_create_cursor_forward(Editor* editor, Command_Source source);
void command_create_cursor_backward(Editor* editor, Command_Source source);

void command_search_forward(Editor* editor, Command_Source source);
void command_search_backward(Editor* editor, Command_Source source);

void command_one_window(Editor* editor, Command_Source source);
void command_split_window_horizontal(Editor* editor, Command_Source source);
void command_split_window_vertical(Editor* editor, Command_Source source);
void command_cycle_window(Editor* editor, Command_Source source);

void command_open_file(Editor* editor, Command_Source source);

}
