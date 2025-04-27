#pragma once

#include <stdint.h>

namespace mag {

struct Contents_Iterator;
struct Token;

namespace syntax {

bool diff_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state);

}
}
