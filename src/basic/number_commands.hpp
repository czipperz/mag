#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace basic {

void command_insert_numbers(Editor* editor, Command_Source source);
void command_increment_numbers(Editor* editor, Command_Source source);
void command_decrement_numbers(Editor* editor, Command_Source source);
void command_prompt_increase_numbers(Editor* editor, Command_Source source);
void command_prompt_multiply_numbers(Editor* editor, Command_Source source);

void command_insert_letters(Editor* editor, Command_Source source);

}
}
