#pragma once

#include "command.hpp"

namespace mag {
namespace basic {
namespace region_movement {

void command_forward_char(Editor* editor, Command_Source source);
void command_backward_char(Editor* editor, Command_Source source);

void command_forward_word(Editor* editor, Command_Source source);
void command_backward_word(Editor* editor, Command_Source source);

void command_forward_paragraph(Editor* editor, Command_Source source);
void command_backward_paragraph(Editor* editor, Command_Source source);

void command_forward_line(Editor* editor, Command_Source source);
void command_backward_line(Editor* editor, Command_Source source);

void command_start_of_line(Editor* editor, Command_Source source);
void command_end_of_line(Editor* editor, Command_Source source);

void command_start_of_line_text(Editor* editor, Command_Source source);
void command_end_of_line_text(Editor* editor, Command_Source source);

void command_start_of_buffer(Editor* editor, Command_Source source);
void command_end_of_buffer(Editor* editor, Command_Source source);

void command_forward_token_pair(Editor* editor, Command_Source source);
void command_backward_token_pair(Editor* editor, Command_Source source);

void command_forward_up_token_pair(Editor* editor, Command_Source source);
void command_backward_up_token_pair(Editor* editor, Command_Source source);

}
}
}
