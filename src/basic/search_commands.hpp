#pragma once

#include <stdint.h>
#include <cz/str.hpp>

namespace mag {
struct Client;
struct Editor;
struct Command_Source;
struct Buffer;
struct Window_Unified;
struct Contents_Iterator;

namespace basic {

bool in_interactive_search(Client* client);
void command_search_forward(Editor* editor, Command_Source source);
void command_search_backward(Editor* editor, Command_Source source);
void command_search_forward_expanding(Editor* editor, Command_Source source);
void command_search_backward_expanding(Editor* editor, Command_Source source);

void command_create_cursor_forward_search(Editor* editor, Command_Source source);
void command_create_cursor_backward_search(Editor* editor, Command_Source source);

void command_search_forward_identifier(Editor* editor, Command_Source source);
void command_search_backward_identifier(Editor* editor, Command_Source source);

int create_cursor_forward_search(const Buffer* buffer, Window_Unified* window);
int create_cursor_backward_search(const Buffer* buffer, Window_Unified* window);

bool search_forward_slice(const Buffer* buffer, Contents_Iterator* start, uint64_t end);
bool search_backward_slice(const Buffer* buffer, Contents_Iterator* start, uint64_t end);

bool find_identifier(Contents_Iterator* iterator, cz::Str query);
bool rfind_identifier(Contents_Iterator* iterator, cz::Str query);

}
}
