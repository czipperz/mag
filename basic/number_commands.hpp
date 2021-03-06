#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_insert_numbers(Editor* editor, Command_Source source);
void command_increment_numbers(Editor* editor, Command_Source source);
void command_decrement_numbers(Editor* editor, Command_Source source);

}
}
