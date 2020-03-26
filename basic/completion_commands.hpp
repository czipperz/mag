#pragma once

#include "command.hpp"
#include "client.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_insert_completion(Editor* editor, Command_Source source);
void command_next_completion(Editor* editor, Command_Source source);
void command_previous_completion(Editor* editor, Command_Source source);

}
}
