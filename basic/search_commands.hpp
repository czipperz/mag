#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_search_reload(Editor* editor, Command_Source source);

void command_search_open(Editor* editor, Command_Source source);

void command_search_open_next(Editor* editor, Command_Source source);
void command_search_open_previous(Editor* editor, Command_Source source);

}
}
