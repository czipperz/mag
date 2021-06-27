#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace version_control {

void command_search(Editor* editor, Command_Source source);
void command_search_token_at_position(Editor* editor, Command_Source source);

}
}
