#pragma once

#include <stdint.h>

namespace mag {

struct Contents;
struct Contents_Iterator;
struct Token;

namespace syntax {

bool build_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state);

bool build_eat_link(Contents_Iterator* it);

}
}
