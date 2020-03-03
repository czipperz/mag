#pragma once

#include <stdint.h>

namespace mag {

struct Contents;
struct Token;

struct Mode {
    bool (*next_token)(const Contents* contents, uint64_t start, Token* token, uint64_t* state);
};

inline bool default_next_token(const Contents* contents,
                               uint64_t start,
                               Token* token,
                               uint64_t* state) {
    return false;
}

}
