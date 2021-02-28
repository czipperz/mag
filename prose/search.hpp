#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace prose {

void command_search_in_current_directory(Editor* editor, Command_Source source);
void command_search_in_current_directory_token_at_position(Editor* editor, Command_Source source);

}
}
