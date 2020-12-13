#pragma once

namespace mag {
struct Editor;
struct Command_Source;
struct Buffer;
struct Contents_Iterator;
struct Token;

namespace basic {

void command_backward_up_pair(Editor* editor, Command_Source source);
void command_forward_up_pair(Editor* editor, Command_Source source);

void command_forward_token_pair(Editor* editor, Command_Source source);
void command_backward_token_pair(Editor* editor, Command_Source source);

void command_forward_matching_token(Editor* editor, Command_Source source);
void command_backward_matching_token(Editor* editor, Command_Source source);

void command_create_cursor_forward_matching_token(Editor* editor, Command_Source source);
void command_create_cursor_backward_matching_token(Editor* editor, Command_Source source);

void backward_up_pair(Buffer* buffer, Contents_Iterator* cursor);
void forward_up_pair(Buffer* buffer, Contents_Iterator* cursor);

void forward_token_pair(Buffer* buffer, Contents_Iterator* iterator);
void backward_token_pair(Buffer* buffer, Contents_Iterator* iterator);

bool forward_matching_token(Buffer* buffer, Contents_Iterator* iterator);
bool backward_matching_token(Buffer* buffer, Contents_Iterator* iterator);

bool find_forward_matching_token(Buffer* buffer,
                                 Contents_Iterator iterator,
                                 Token* this_token,
                                 Token* matching_token);
bool find_backward_matching_token(Buffer* buffer,
                                  Contents_Iterator iterator,
                                  Token* this_token,
                                  Token* matching_token);

}
}
