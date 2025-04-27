#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace basic {

void command_shift_line_forward(Editor* editor, Command_Source source);
void command_shift_line_backward(Editor* editor, Command_Source source);

}
}
