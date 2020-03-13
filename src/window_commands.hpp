#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {

struct Window;

Window* window_first(Window* window);

void command_one_window(Editor* editor, Command_Source source);
void command_split_window_horizontal(Editor* editor, Command_Source source);
void command_split_window_vertical(Editor* editor, Command_Source source);
void command_cycle_window(Editor* editor, Command_Source source);

}
