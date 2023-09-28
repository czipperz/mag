#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace version_control {

void command_show_last_commit_to_file(Editor* editor, Command_Source source);
void command_show_commit(Editor* editor, Command_Source source);
void command_show_commit_at_sol(Editor* editor, Command_Source source);

void command_line_history(Editor* editor, Command_Source source);

}
}
