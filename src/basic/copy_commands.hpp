#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace basic {

void command_cut(Editor* editor, Command_Source source);
void command_copy(Editor* editor, Command_Source source);
void command_paste(Editor* editor, Command_Source source);
void command_paste_previous(Editor* editor, Command_Source source);

void command_cursors_cut_as_lines(Editor* editor, Command_Source source);
void command_cursors_copy_as_lines(Editor* editor, Command_Source source);
void command_cursors_paste_as_lines(Editor* editor, Command_Source source);
void command_cursors_paste_previous_as_lines(Editor* editor, Command_Source source);

void command_copy_selected_region_length(Editor* editor, Command_Source source);

}
}
