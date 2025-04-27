#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_search_buffer_reload(Editor* editor, Command_Source source);

/// From the search window use one of these commands to
/// open a result and stay on the search window.
void command_search_buffer_open_selected_no_swap(Editor* editor, Command_Source source);
void command_search_buffer_open_next_no_swap(Editor* editor, Command_Source source);
void command_search_buffer_open_previous_no_swap(Editor* editor, Command_Source source);

/// From the search window use one of these commands
/// to open a result and switch to the result window.
void command_search_buffer_open_selected(Editor* editor, Command_Source source);
void command_search_buffer_open_next(Editor* editor, Command_Source source);
void command_search_buffer_open_previous(Editor* editor, Command_Source source);

/// From the result window use one of these commands to switch to a different result.
void command_search_buffer_continue_selected(Editor* editor, Command_Source source);
void command_search_buffer_continue_next(Editor* editor, Command_Source source);
void command_search_buffer_continue_previous(Editor* editor, Command_Source source);

}
}
