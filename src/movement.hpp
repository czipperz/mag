#pragma once

#include <stdint.h>

namespace mag {

struct Buffer;
struct Contents_Iterator;

void start_of_line(Contents_Iterator* iterator);
void end_of_line(Contents_Iterator* iterator);

void start_of_line_text(Contents_Iterator* iterator);

void forward_line(Contents_Iterator* iterator);
void backward_line(Contents_Iterator* iterator);

void forward_word(Contents_Iterator* iterator);
void backward_word(Contents_Iterator* iterator);

void forward_char(Contents_Iterator* iterator);
void backward_char(Contents_Iterator* iterator);

}
