#pragma once

#include <stdint.h>

namespace mag {

struct Buffer;
struct Contents_Iterator;

void start_of_line(Buffer* buffer, Contents_Iterator* iterator);
void end_of_line(Buffer* buffer, Contents_Iterator* iterator);

void start_of_line_text(Buffer* buffer, Contents_Iterator* iterator);

void forward_line(Buffer* buffer, Contents_Iterator* iterator);
void backward_line(Buffer* buffer, Contents_Iterator* iterator);

void forward_word(Buffer* buffer, Contents_Iterator* iterator);
void backward_word(Buffer* buffer, Contents_Iterator* iterator);

void forward_char(Buffer* buffer, Contents_Iterator* iterator);
void backward_char(Buffer* buffer, Contents_Iterator* iterator);

}
