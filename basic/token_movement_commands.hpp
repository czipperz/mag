#pragma once

namespace mag {
struct Editor;
struct Command_Source;

namespace basic {

void command_backward_up_pair(Editor* editor, Command_Source source);
void command_forward_up_pair(Editor* editor, Command_Source source);

void command_forward_token_pair(Editor* editor, Command_Source source);
void command_backward_token_pair(Editor* editor, Command_Source source);

}
}
