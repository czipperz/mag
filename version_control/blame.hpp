#pragma once

#include "editor.hpp"
#include "command.hpp"

namespace mag {
namespace version_control {

void command_blame(Editor* editor, Command_Source source);

bool git_blame_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state);

}
}
