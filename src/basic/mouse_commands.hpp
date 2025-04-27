#pragma once

#include "core/client.hpp"
#include "core/editor.hpp"

namespace mag {
namespace basic {

void command_mouse_select_start(Editor* editor, Command_Source source);
void command_mouse_select_continue(Editor* editor, Command_Source source);
void command_copy_paste(Editor* editor, Command_Source source);

}
}
