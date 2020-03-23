#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace custom {

void command_shift_line_forward(Editor* editor, Command_Source source);
void command_shift_line_backward(Editor* editor, Command_Source source);

}
}
