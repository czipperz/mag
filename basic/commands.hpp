#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_toggle_read_only(Editor* editor, Command_Source source);
void command_toggle_pinned(Editor* editor, Command_Source source);

void command_set_mark(Editor* editor, Command_Source source);
void command_swap_mark_point(Editor* editor, Command_Source source);

void command_forward_char(Editor* editor, Command_Source source);
void command_backward_char(Editor* editor, Command_Source source);

void command_forward_word(Editor* editor, Command_Source source);
void command_backward_word(Editor* editor, Command_Source source);

void command_forward_line(Editor* editor, Command_Source source);
void command_backward_line(Editor* editor, Command_Source source);

void command_end_of_buffer(Editor* editor, Command_Source source);
void command_start_of_buffer(Editor* editor, Command_Source source);

void command_push_jump(Editor* editor, Command_Source source);
void command_unpop_jump(Editor* editor, Command_Source source);
void command_pop_jump(Editor* editor, Command_Source source);

void command_end_of_line(Editor* editor, Command_Source source);
void command_start_of_line(Editor* editor, Command_Source source);

void command_start_of_line_text(Editor* editor, Command_Source source);

void command_delete_backward_char(Editor* editor, Command_Source source);
void command_delete_forward_char(Editor* editor, Command_Source source);

void command_delete_backward_word(Editor* editor, Command_Source source);
void command_delete_forward_word(Editor* editor, Command_Source source);

void command_transpose_characters(Editor* editor, Command_Source source);

void command_open_line(Editor* editor, Command_Source source);
void command_insert_newline_no_indent(Editor* editor, Command_Source source);

void command_duplicate_line(Editor* editor, Command_Source source);
void command_delete_line(Editor* editor, Command_Source source);
void command_delete_end_of_line(Editor* editor, Command_Source source);

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

void command_create_cursors_all_search(Editor* editor, Command_Source source);
void command_create_cursors_to_end_search(Editor* editor, Command_Source source);
void command_create_cursors_to_start_search(Editor* editor, Command_Source source);

void command_create_cursors_undo(Editor* editor, Command_Source source);
void command_create_cursors_redo(Editor* editor, Command_Source source);
void command_create_cursors_last_change(Editor* editor, Command_Source source);

void command_create_cursors_lines_in_region(Editor* editor, Command_Source source);

void command_cursors_align(Editor* editor, Command_Source source);

void command_search_forward(Editor* editor, Command_Source source);
void command_search_backward(Editor* editor, Command_Source source);

void command_goto_line(Editor* editor, Command_Source source);
void command_goto_position(Editor* editor, Command_Source source);

void command_path_up_directory(Editor* editor, Command_Source source);

void command_mark_buffer(Editor* editor, Command_Source source);

void command_submit_mini_buffer(Editor* editor, Command_Source source);

}
}
