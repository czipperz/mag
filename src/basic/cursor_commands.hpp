#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace basic {

void show_created_messages(Client* client, int created);

void command_create_cursor_forward_line(Editor* editor, Command_Source source);
void command_create_cursor_backward_line(Editor* editor, Command_Source source);
void command_create_cursor_forward(Editor* editor, Command_Source source);
void command_create_cursor_backward(Editor* editor, Command_Source source);

void command_create_cursors_all_search(Editor* editor, Command_Source source);
void command_create_cursors_to_end_search(Editor* editor, Command_Source source);
void command_create_cursors_to_start_search(Editor* editor, Command_Source source);

void command_filter_cursors_looking_at(Editor* editor, Command_Source source);
void command_filter_cursors_not_looking_at(Editor* editor, Command_Source source);

void command_create_cursors_undo(Editor* editor, Command_Source source);
void command_create_cursors_undo_nono(Editor* editor, Command_Source source);
void command_create_cursors_redo(Editor* editor, Command_Source source);
void command_create_cursors_redo_nono(Editor* editor, Command_Source source);
void command_create_cursors_last_change(Editor* editor, Command_Source source);

void command_create_cursors_lines_in_region(Editor* editor, Command_Source source);

void command_cursors_align(Editor* editor, Command_Source source);
void command_cursors_align_leftpad0(Editor* editor, Command_Source source);
void command_cursors_align_leftpad_spaces(Editor* editor, Command_Source source);

void command_remove_cursors_at_empty_lines(Editor* editor, Command_Source source);
void command_remove_selected_cursor(Editor* editor, Command_Source source);

void command_remove_even_cursors(Editor* editor, Command_Source source);
void command_remove_odd_cursors(Editor* editor, Command_Source source);

}
}
