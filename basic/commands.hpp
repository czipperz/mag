#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_do_nothing(Editor* editor, Command_Source source);
void command_invalid(Editor* editor, Command_Source source);

void command_show_marks(Editor* editor, Command_Source source);
void command_set_mark(Editor* editor, Command_Source source);
void command_swap_mark_point(Editor* editor, Command_Source source);

void command_push_jump(Editor* editor, Command_Source source);
void command_unpop_jump(Editor* editor, Command_Source source);
void command_pop_jump(Editor* editor, Command_Source source);

void command_delete_backward_char(Editor* editor, Command_Source source);
void command_delete_forward_char(Editor* editor, Command_Source source);

void command_delete_backward_word(Editor* editor, Command_Source source);
void command_delete_forward_word(Editor* editor, Command_Source source);

void command_transpose_characters(Editor* editor, Command_Source source);
void command_transpose_words(Editor* editor, Command_Source source);

void command_open_line(Editor* editor, Command_Source source);
void command_insert_newline_no_indent(Editor* editor, Command_Source source);

void command_duplicate_line(Editor* editor, Command_Source source);
void command_duplicate_line_prompt(Editor* editor, Command_Source source);
void command_delete_line(Editor* editor, Command_Source source);
void command_delete_end_of_line(Editor* editor, Command_Source source);
void command_delete_start_of_line(Editor* editor, Command_Source source);
void command_delete_start_of_line_text(Editor* editor, Command_Source source);

void command_fill_region_with_spaces(Editor* editor, Command_Source source);
void command_fill_region_or_solt_with_spaces(Editor* editor, Command_Source source);

void command_undo(Editor* editor, Command_Source source);
void command_redo(Editor* editor, Command_Source source);

void command_stop_action(Editor* editor, Command_Source source);

void command_quit(Editor* editor, Command_Source source);

void command_goto_line(Editor* editor, Command_Source source);
void command_goto_position(Editor* editor, Command_Source source);
void command_goto_column(Editor* editor, Command_Source source);
void command_show_file_length_info(Editor* editor, Command_Source source);

void command_path_up_directory(Editor* editor, Command_Source source);

void command_mark_buffer(Editor* editor, Command_Source source);

void submit_mini_buffer(Editor* editor, Client* client);
void command_submit_mini_buffer(Editor* editor, Command_Source source);

void command_insert_home_directory(Editor* editor, Command_Source source);

void command_increase_font_size(Editor* editor, Command_Source source);
void command_decrease_font_size(Editor* editor, Command_Source source);

void command_show_date_of_build(Editor* editor, Command_Source source);

void command_comment_hash(Editor* editor, Command_Source source);
void command_uncomment_hash(Editor* editor, Command_Source source);

void command_sort_lines_ascending(Editor* editor, Command_Source source);
void command_sort_lines_descending(Editor* editor, Command_Source source);
void command_sort_lines_ascending_shortlex(Editor* editor, Command_Source source);
void command_sort_lines_descending_shortlex(Editor* editor, Command_Source source);
void command_flip_lines(Editor* editor, Command_Source source);

void command_restore_last_save_point(Editor* editor, Command_Source source);
void command_undo_all(Editor* editor, Command_Source source);
void command_redo_all(Editor* editor, Command_Source source);

void command_run_command_for_result(Editor* editor, Command_Source source);
void command_run_command_ignore_result(Editor* editor, Command_Source source);

void command_replace_region(Editor* editor, Command_Source source);
void command_insert_num(Editor* editor, Command_Source source);

void insert_divider_helper(Editor* editor, Command_Source source, char ch, uint64_t target_column);

}
}
