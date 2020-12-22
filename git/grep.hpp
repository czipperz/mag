#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace git {

void command_git_grep(Editor* editor, Command_Source source);
void command_git_grep_token_at_position(Editor* editor, Command_Source source);

}
}
