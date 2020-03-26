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

void command_center_in_window(Editor* editor, Command_Source source);
void command_goto_center_of_window(Editor* editor, Command_Source source);

}
}
