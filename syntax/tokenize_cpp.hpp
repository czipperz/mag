#pragma once

#include <stdint.h>

namespace mag {

struct Contents;
struct Contents_Iterator;
struct Token;

namespace custom {

bool cpp_next_token(const mag::Contents* contents,
                    mag::Contents_Iterator* iterator,
                    mag::Token* token,
                    uint64_t* state);

}
}
