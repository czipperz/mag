#pragma once

#include <stdint.h>

namespace mag {

struct Buffer;
struct Contents;
struct Contents_Iterator;
struct Mode;
struct SSOStr;
struct Theme;
struct Token;
struct Window_Unified;

void start_of_line(Contents_Iterator* iterator);
void end_of_line(Contents_Iterator* iterator);

void start_of_line_text(Contents_Iterator* iterator);
void end_of_line_text(Contents_Iterator* iterator);

void forward_through_whitespace(Contents_Iterator* iterator);
void backward_through_whitespace(Contents_Iterator* iterator);

/// Visible lines are either wrapped at `window->cols()` or terminated by a newline.
void start_of_visual_line(const Window_Unified*, const Mode&, const Theme&, Contents_Iterator*);
void end_of_visual_line(const Window_Unified*, const Mode&, const Theme&, Contents_Iterator*);
void forward_visual_line(const Window_Unified* window,
                         const Mode& mode,
                         const Theme& theme,
                         Contents_Iterator* it,
                         uint64_t rows = 1);
void backward_visual_line(const Window_Unified* window,
                          const Mode& mode,
                          const Theme& theme,
                          Contents_Iterator* it,
                          uint64_t rows = 1);

uint64_t char_visual_columns(const Mode& mode, char ch, uint64_t column);
uint64_t count_visual_columns(const Mode& mode,
                              Contents_Iterator start,
                              uint64_t end,
                              uint64_t initial_column = 0);
uint64_t get_visual_column(const Mode& mode, Contents_Iterator iterator);
void go_to_visual_column(const Mode& mode, Contents_Iterator* iterator, uint64_t column);
void analyze_indent(const Mode& mode, uint64_t columns, uint64_t* num_tabs, uint64_t* num_spaces);

bool forward_line(const Mode& mode, Contents_Iterator* iterator, uint64_t lines = 1);
bool backward_line(const Mode& mode, Contents_Iterator* iterator, uint64_t lines = 1);

void forward_word(Contents_Iterator* iterator);
void backward_word(Contents_Iterator* iterator);

void forward_char(Contents_Iterator* iterator);
void backward_char(Contents_Iterator* iterator);

void forward_through_identifier(Contents_Iterator* iterator);
void backward_through_identifier(Contents_Iterator* iterator);

void forward_paragraph(Contents_Iterator* iterator);
void backward_paragraph(Contents_Iterator* iterator);

Contents_Iterator start_of_line_position(const Contents& contents, uint64_t lines);
Contents_Iterator iterator_at_line_column(const Contents& contents, uint64_t line, uint64_t column);

/// Get the `token` at the `iterator`'s position, if there is one.  Positions the `iterator` at the
/// start of the `token`.
bool get_token_at_position(const Buffer* buffer, Contents_Iterator* iterator, Token* token);

bool get_token_at_position_contents(const Buffer* buffer, uint64_t position, SSOStr* contents);

bool at_start_of_line(Contents_Iterator iterator);
bool at_end_of_line(Contents_Iterator iterator);
bool at_empty_line(Contents_Iterator iterator);

}
