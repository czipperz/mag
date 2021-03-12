#pragma once

#include "command.hpp"
#include "editor.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

void command_center_in_window(Editor* editor, Command_Source source);
void command_goto_center_of_window(Editor* editor, Command_Source source);

void command_up_page(Editor* editor, Command_Source source);
void command_down_page(Editor* editor, Command_Source source);

void command_scroll_down(Editor* editor, Command_Source source);
void command_scroll_up(Editor* editor, Command_Source source);

void command_scroll_down_one(Editor* editor, Command_Source source);
void command_scroll_up_one(Editor* editor, Command_Source source);

}
}
