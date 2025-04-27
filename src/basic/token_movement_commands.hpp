#pragma once

#include <stdint.h>

namespace mag {
struct Editor;
struct Command_Source;
struct Buffer;
struct Contents_Iterator;
struct Token;

namespace basic {

void command_backward_up_token_pair(Editor* editor, Command_Source source);
void command_forward_up_token_pair(Editor* editor, Command_Source source);

void command_backward_up_token_pair_or_indent(Editor* editor, Command_Source source);
void command_forward_up_token_pair_or_indent(Editor* editor, Command_Source source);

void command_forward_token_pair(Editor* editor, Command_Source source);
void command_backward_token_pair(Editor* editor, Command_Source source);

void command_forward_matching_token(Editor* editor, Command_Source source);
void command_backward_matching_token(Editor* editor, Command_Source source);

void command_create_cursor_forward_matching_token(Editor* editor, Command_Source source);
void command_create_cursor_backward_matching_token(Editor* editor, Command_Source source);

void command_create_cursors_to_end_matching_token(Editor* editor, Command_Source source);
void command_create_cursors_to_start_matching_token(Editor* editor, Command_Source source);
void command_create_all_cursors_matching_token(Editor* editor, Command_Source source);

void command_create_all_cursors_matching_token_or_search(Editor* editor, Command_Source source);

void command_delete_token(Editor* editor, Command_Source source);
void command_duplicate_token(Editor* editor, Command_Source source);
void command_transpose_tokens(Editor* editor, Command_Source source);

void command_delete_backward_token(Editor* editor, Command_Source source);
void command_delete_forward_token(Editor* editor, Command_Source source);

bool backward_up_token_pair(Buffer* buffer, Contents_Iterator* cursor, bool non_pair);
bool forward_up_token_pair(Buffer* buffer, Contents_Iterator* cursor, bool non_pair);

bool backward_up_token_pair_or_indent(Buffer* buffer, Contents_Iterator* cursor, bool non_pairs);
bool forward_up_token_pair_or_indent(Buffer* buffer, Contents_Iterator* cursor, bool non_pairs);

bool forward_token_pair(Buffer* buffer, Contents_Iterator* iterator, bool non_pair);
bool backward_token_pair(Buffer* buffer, Contents_Iterator* iterator, bool non_pair);

int forward_matching_token(Buffer* buffer, Contents_Iterator* iterator);
int backward_matching_token(Buffer* buffer, Contents_Iterator* iterator);

int find_forward_matching_token(Buffer* buffer,
                                Contents_Iterator iterator,
                                Token* this_token,
                                Token* matching_token);
int find_backward_matching_token(Buffer* buffer,
                                 Contents_Iterator iterator,
                                 Token* this_token,
                                 Token* matching_token);

bool get_token_before_position(Buffer* buffer,
                               Contents_Iterator* token_iterator,
                               uint64_t* state,
                               Token* token);

bool get_token_after_position(Buffer* buffer,
                              Contents_Iterator* token_iterator,
                              uint64_t* state,
                              Token* token);

}
}
