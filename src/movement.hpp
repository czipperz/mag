#pragma once

#include <stdint.h>

namespace mag {

struct Buffer;

uint64_t start_of_line(Buffer* buffer, uint64_t point);
uint64_t end_of_line(Buffer* buffer, uint64_t point);

uint64_t forward_line(Buffer* buffer, uint64_t point);
uint64_t backward_line(Buffer* buffer, uint64_t point);

uint64_t forward_word(Buffer* buffer, uint64_t point);
uint64_t backward_word(Buffer* buffer, uint64_t point);

uint64_t forward_char(Buffer* buffer, uint64_t point);
uint64_t backward_char(Buffer* buffer, uint64_t point);

}
