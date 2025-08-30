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
    uint64_t state;
    const Contents_Iterator& iterator_at_tokenization_position() const { return iterator_; }

    bool has_token() const;
    const Token& token() const;  /// Note: asserts the token is valid.
    Contents_Iterator iterator_at_token_start() const; /// Note: asserts the token is valid.

private:
    Token token_;
    Contents_Iterator iterator_;
};
}
