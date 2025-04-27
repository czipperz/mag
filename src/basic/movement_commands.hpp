#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_forward_char(Editor* editor, Command_Source source);
void command_backward_char(Editor* editor, Command_Source source);

void command_forward_word(Editor* editor, Command_Source source);
void command_backward_word(Editor* editor, Command_Source source);

void command_forward_line(Editor* editor, Command_Source source);
void command_backward_line(Editor* editor, Command_Source source);

void command_forward_line_single_cursor_visual(Editor* editor, Command_Source source);
void command_backward_line_single_cursor_visual(Editor* editor, Command_Source source);

void command_forward_paragraph(Editor* editor, Command_Source source);
void command_backward_paragraph(Editor* editor, Command_Source source);

void command_end_of_buffer(Editor* editor, Command_Source source);
void command_start_of_buffer(Editor* editor, Command_Source source);

void command_end_of_line(Editor* editor, Command_Source source);
void command_start_of_line(Editor* editor, Command_Source source);

void command_end_of_line_text(Editor* editor, Command_Source source);
void command_start_of_line_text(Editor* editor, Command_Source source);

}
}
