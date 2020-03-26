#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_uppercase_letter(Editor* editor, Command_Source source);
void command_lowercase_letter(Editor* editor, Command_Source source);

void command_uppercase_region(Editor* editor, Command_Source source);
void command_lowercase_region(Editor* editor, Command_Source source);

}
}
