#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {
namespace window_completion {

void command_finish_completion(Editor* editor, Command_Source source);

void command_next_completion(Editor* editor, Command_Source source);
void command_previous_completion(Editor* editor, Command_Source source);

void command_completion_down_page(Editor* editor, Command_Source source);
void command_completion_up_page(Editor* editor, Command_Source source);

void command_first_completion(Editor* editor, Command_Source source);
void command_last_completion(Editor* editor, Command_Source source);

}
}
}
