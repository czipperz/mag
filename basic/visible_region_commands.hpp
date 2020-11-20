#pragma once

#include "command.hpp"
#include "editor.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

/// Adjust the window such that the iterator is at the center.
void center_in_window(Window_Unified* window, Contents_Iterator iterator);

/// Get an iterator to the center of the window.
Contents_Iterator center_of_window(Window_Unified* window, const Contents* contents);

/// Test if the iterator is inside the visible region of the window.
bool is_visible(Window_Unified* window, Contents_Iterator iterator);

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
