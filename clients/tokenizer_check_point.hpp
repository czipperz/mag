#pragma once

#include <stdint.h>
#include <cz/vector.hpp>
#include "buffer.hpp"
#include "contents.hpp"

namespace mag {
namespace client {

struct Tokenizer_Check_Point {
    uint64_t position;
    uint64_t state;
};

struct Window_Cache;

bool next_check_point(Window_Cache* window_cache,
                      Buffer* buffer,
                      Contents_Iterator* iterator,
                      uint64_t* state,
                      cz::Vector<Tokenizer_Check_Point>* check_points);

}
}
