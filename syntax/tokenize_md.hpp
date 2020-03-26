#pragma once

#include <stdint.h>

namespace mag {

struct Contents;
struct Contents_Iterator;
struct Token;

namespace syntax {

bool md_next_token(const Contents* contents,
                   Contents_Iterator* iterator,
                   Token* token,
                   uint64_t* state);

}
}
