#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace custom {

void command_shift_line_forward(mag::Editor* editor, mag::Command_Source source);
void command_shift_line_backward(mag::Editor* editor, mag::Command_Source source);

}
