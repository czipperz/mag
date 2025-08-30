#pragma once

#include "core/buffer.hpp"
#include "core/token.hpp"

namespace mag {
/// Note: call buffer.token_cache.update() before initializing.
struct Forward_Token_Iterator {
    void init_at_check_point(const Buffer* buffer, uint64_t position);
    bool init_at_or_after(const Buffer* buffer, uint64_t position);
    bool init_after(const Buffer* buffer, uint64_t position);

    bool next();
    bool find_at_or_after(uint64_t position);
    bool find_after(uint64_t position);
    bool find_type(Token_Type type);

    Tokenizer tokenizer;
    Contents_Iterator iterator;
    Token token;  /// Note: check if a token has been found via Token::is_valid().
    uint64_t state;
};
}
