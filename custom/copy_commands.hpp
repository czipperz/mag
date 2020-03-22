#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace custom {

void command_cut(mag::Editor* editor, mag::Command_Source source);
void command_copy(mag::Editor* editor, mag::Command_Source source);
void command_paste(mag::Editor* editor, mag::Command_Source source);

}
