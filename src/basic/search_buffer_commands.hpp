#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
struct Window_Unified;
namespace basic {

bool iterate_cursors(Window_Unified* window,
                     const Buffer* buffer,
                     bool select_next,
                     Contents_Iterator* it);

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

void command_search_buffer_next_file(Editor* editor, Command_Source source);
void command_search_buffer_previous_file(Editor* editor, Command_Source source);

void search_buffer_iterate(Editor* editor, Client* client, bool select_next);

}
}
