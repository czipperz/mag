#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
struct Window;
struct Window_Unified;
}

namespace custom {

mag::Window_Unified* window_first(mag::Window* window);

void command_one_window(mag::Editor* editor, mag::Command_Source source);
void command_split_window_horizontal(mag::Editor* editor, mag::Command_Source source);
void command_split_window_vertical(mag::Editor* editor, mag::Command_Source source);
void command_close_window(mag::Editor* editor, mag::Command_Source source);
void command_cycle_window(mag::Editor* editor, mag::Command_Source source);

}
