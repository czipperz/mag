#pragma once

#include <cz/heap_vector.hpp>
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

    bool has_token() const;
    const Token& token() const;                         /// Note: asserts the token is valid.
    Contents_Iterator iterator_at_token_start() const;  /// Note: asserts the token is valid.

    Tokenizer tokenizer;
    uint64_t state;
    Contents_Iterator tokenization_iterator;

private:
    Token token_;
};

struct Backward_Token_Iterator {
    void drop();

    bool init_at_or_before(const Buffer* buffer, uint64_t position);

    Forward_Token_Iterator jump_to_check_point(uint64_t position);
    bool cache_until(Forward_Token_Iterator it, uint64_t position);
    bool cache_previous_check_point();

    bool previous();
    bool rfind_type(Token_Type type);

    bool has_token() const;
    const Token& token() const;                         /// Note: asserts the token is valid.
    Contents_Iterator iterator_at_token_start() const;  /// Note: asserts the token is valid.

    Contents_Iterator check_point_iterator;
    cz::Heap_Vector<Token> tokens_since_check_point;

private:
    const Buffer* buffer_;
    Token token_;
};
}
