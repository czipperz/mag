#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace prose {

void command_search_in_current_directory_prompt(Editor* editor, Command_Source source);
void command_search_in_current_directory_token_at_position(Editor* editor, Command_Source source);
void command_search_in_current_directory_word_prompt(Editor* editor, Command_Source source);

void command_search_in_version_control_prompt(Editor* editor, Command_Source source);
void command_search_in_version_control_token_at_position(Editor* editor, Command_Source source);
void command_search_in_version_control_word_prompt(Editor* editor, Command_Source source);

void command_search_in_file_prompt(Editor* editor, Command_Source source);
void command_search_in_file_token_at_position(Editor* editor, Command_Source source);
void command_search_in_file_word_prompt(Editor* editor, Command_Source source);

void command_search_conflicts(Editor* editor, Command_Source source);

}
}
