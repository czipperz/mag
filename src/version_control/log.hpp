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
void command_git_log_next_file(Editor* editor, Command_Source source);
void command_git_log_previous_file(Editor* editor, Command_Source source);
void command_git_log_next_diff(Editor* editor, Command_Source source);
void command_git_log_previous_diff(Editor* editor, Command_Source source);

/// Open diff in a split or commit in the current window.
void command_git_log_open_selected_commit_or_diff(Editor* editor, Command_Source source);

/// Open diff in a split.
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
void command_file_history_filtered(Editor* editor, Command_Source source);
void command_line_history(Editor* editor, Command_Source source);

/// Open a new buffer that filters the shown results via -G.
void command_git_log_add_filter(Editor* editor, Command_Source source);
/// Open a new buffer that adds `--follow` to the command line.
void command_git_log_add_follow(Editor* editor, Command_Source source);

void command_git_diff_master(Editor* editor, Command_Source source);
void command_git_diff_master_this_file(Editor* editor, Command_Source source);

void command_git_diff_add_ignore_whitespace(Editor* editor, Command_Source source);
void command_git_diff_remove_ignore_whitespace(Editor* editor, Command_Source source);

}
}
