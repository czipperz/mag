#pragma once

#include "client.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_mouse_select_start(Editor* editor, Command_Source source);
void command_copy_paste(Editor* editor, Command_Source source);

}
}
