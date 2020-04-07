#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {

struct Window;
struct Window_Unified;

namespace basic {

Window_Unified* window_first(Window* window);
Window_Unified* window_last(Window* window);
void cycle_window(Client* client);
void reverse_cycle_window(Client* client);

void command_one_window(Editor* editor, Command_Source source);
void command_split_window_horizontal(Editor* editor, Command_Source source);
void command_split_window_vertical(Editor* editor, Command_Source source);
void command_close_window(Editor* editor, Command_Source source);
void command_cycle_window(Editor* editor, Command_Source source);
void command_reverse_cycle_window(Editor* editor, Command_Source source);

}
}
