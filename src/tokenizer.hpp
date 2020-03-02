#pragma once

#include <stdint.h>

namespace mag {

struct Contents;
struct Token;

struct Tokenizer {
    bool (*next_token)(const Contents* contents, uint64_t start, Token* token);
};

inline bool default_next_token(const Contents* contents, uint64_t start, Token* token) {
    return false;
}

}
