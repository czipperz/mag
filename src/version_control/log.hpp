#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace version_control {

/// Show an individual commit.
void command_show_last_commit_to_file(Editor* editor, Command_Source source);
void command_show_commit(Editor* editor, Command_Source source);
void command_show_commit_in_blame(Editor* editor, Command_Source source);
void command_show_commit_in_log(Editor* editor, Command_Source source);

/// Movement commands.
void command_git_log_next_commit(Editor* editor, Command_Source source);
void command_git_log_previous_commit(Editor* editor, Command_Source source);
void command_git_log_next_diff(Editor* editor, Command_Source source);
void command_git_log_previous_diff(Editor* editor, Command_Source source);

/// Open the next patch using a split.
void command_git_log_open_selected_diff(Editor* editor, Command_Source source);
void command_git_log_open_next_diff(Editor* editor, Command_Source source);
void command_git_log_open_previous_diff(Editor* editor, Command_Source source);
void command_git_log_open_selected_diff_no_swap(Editor* editor, Command_Source source);
void command_git_log_open_next_diff_no_swap(Editor* editor, Command_Source source);
void command_git_log_open_previous_diff_no_swap(Editor* editor, Command_Source source);
void log_buffer_iterate(Editor* editor, Client* client, bool select_next);

/// Show git log.
void command_git_log(Editor* editor, Command_Source source);
void command_file_history(Editor* editor, Command_Source source);
void command_line_history(Editor* editor, Command_Source source);

/// Open a new buffer that filters the shown results via -G.
void command_git_log_add_filter(Editor* editor, Command_Source source);

}
}
