#pragma once

#include <stdint.h>

namespace mag {

struct Contents;
struct Contents_Iterator;
struct Token;

namespace syntax {

bool md_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state);

bool md_next_token_stop_at_hash_comment(Contents_Iterator* iterator, Token* token, uint64_t* state);

}
}
