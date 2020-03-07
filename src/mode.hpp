#pragma once

#include <stdint.h>

namespace mag {

struct Contents;
struct Contents_Iterator;
struct Token;

struct Mode {
    bool (*next_token)(const Contents* contents /* in */,
                       Contents_Iterator* iterator /* in/out */,
                       Token* token /* out */,
                       uint64_t* state /* in/out */);
};

inline bool default_next_token(const Contents* contents,
                               Contents_Iterator* iterator,
                               Token* token,
                               uint64_t* state) {
    return false;
}

}
