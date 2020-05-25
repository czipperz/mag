#pragma once

namespace mag {
struct Editor;
struct Command_Source;
struct Buffer;
struct Contents_Iterator;

namespace basic {

void command_backward_up_pair(Editor* editor, Command_Source source);
void command_forward_up_pair(Editor* editor, Command_Source source);

void command_forward_token_pair(Editor* editor, Command_Source source);
void command_backward_token_pair(Editor* editor, Command_Source source);

void backward_up_pair(Buffer* buffer, Contents_Iterator* cursor);
void forward_up_pair(Buffer* buffer, Contents_Iterator* cursor);

void forward_token_pair(Buffer* buffer, Contents_Iterator* iterator);
void backward_token_pair(Buffer* buffer, Contents_Iterator* iterator);

}
}
