#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace version_control {

void command_show_last_commit_to_file(Editor* editor, Command_Source source);
void command_show_commit(Editor* editor, Command_Source source);
void command_show_commit_in_blame(Editor* editor, Command_Source source);
void command_show_commit_in_log(Editor* editor, Command_Source source);

void command_git_log_next_commit(Editor* editor, Command_Source source);
void command_git_log_previous_commit(Editor* editor, Command_Source source);

void command_git_log(Editor* editor, Command_Source source);
void command_file_history(Editor* editor, Command_Source source);
void command_line_history(Editor* editor, Command_Source source);

}
}
