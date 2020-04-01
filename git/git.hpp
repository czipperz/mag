#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace git {

void command_git_grep(Editor* editor, Command_Source source);

void command_save_and_quit(Editor* editor, Command_Source source);
void command_abort_and_quit(Editor* editor, Command_Source source);

}
}
