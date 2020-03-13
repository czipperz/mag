#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {

void command_open_file(Editor* editor, Command_Source source);
void command_save_file(Editor* editor, Command_Source source);

}
