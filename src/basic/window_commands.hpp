#pragma once

#include "command.hpp"
#include "editor.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

void command_one_window(Editor* editor, Command_Source source);
void command_one_window_except_pinned(Editor* editor, Command_Source source);
void command_split_window_horizontal(Editor* editor, Command_Source source);
void command_split_window_vertical(Editor* editor, Command_Source source);
void command_split_increase_ratio(Editor* editor, Command_Source source);
void command_split_decrease_ratio(Editor* editor, Command_Source source);
void command_split_reset_ratio(Editor* editor, Command_Source source);
void command_close_window(Editor* editor, Command_Source source);
void command_quit_window(Editor* editor, Command_Source source);
void command_cycle_window(Editor* editor, Command_Source source);
void command_reverse_cycle_window(Editor* editor, Command_Source source);

void command_swap_windows(Editor* editor, Command_Source source);
void command_shift_window_up(Editor* editor, Command_Source source);
void command_shift_window_down(Editor* editor, Command_Source source);
void command_shift_window_left(Editor* editor, Command_Source source);
void command_shift_window_right(Editor* editor, Command_Source source);

}
}
