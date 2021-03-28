#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_cut(Editor* editor, Command_Source source);
void command_copy(Editor* editor, Command_Source source);
void command_paste(Editor* editor, Command_Source source);
void command_paste_previous(Editor* editor, Command_Source source);

void command_copy_selected_region_length(Editor* editor, Command_Source source);

}
}
