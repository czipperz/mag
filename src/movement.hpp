#pragma once

#include <stdint.h>

namespace mag {

struct Buffer;
struct Contents_Iterator;
struct Contents;
struct Token;
struct Theme;

void start_of_line(Contents_Iterator* iterator);
void end_of_line(Contents_Iterator* iterator);

void start_of_line_text(Contents_Iterator* iterator);

void forward_through_whitespace(Contents_Iterator* iterator);
void backward_through_whitespace(Contents_Iterator* iterator);

uint64_t get_current_column(const Theme& theme, Contents_Iterator iterator);
void go_to_column(const Theme& theme, Contents_Iterator* iterator, uint64_t column);

void forward_line(const Theme& theme, Contents_Iterator* iterator);
void backward_line(const Theme& theme, Contents_Iterator* iterator);

void forward_word(Contents_Iterator* iterator);
void backward_word(Contents_Iterator* iterator);

void forward_char(Contents_Iterator* iterator);
void backward_char(Contents_Iterator* iterator);

Contents_Iterator start_of_line_position(const Contents& contents, uint64_t lines);

/// Get the `token` at the `iterator`'s position, if there is one.  Positions the `iterator` at the
/// start of the `token`.
bool get_token_at_position(Buffer* buffer, Contents_Iterator* iterator, Token* token);

}
