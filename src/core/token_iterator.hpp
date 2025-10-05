#pragma once

#include <cz/vector.hpp>
#include "core/buffer.hpp"
#include "core/token.hpp"

namespace mag {
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
    void drop(cz::Allocator allocator);

    bool init_at_or_before(cz::Allocator allocator, const Buffer* buffer, uint64_t position);
    bool init_before(cz::Allocator allocator, const Buffer* buffer, uint64_t position);

    Forward_Token_Iterator jump_to_check_point(uint64_t position);
    bool cache_until(cz::Allocator allocator, Forward_Token_Iterator it, uint64_t position);
    bool cache_previous_check_point(cz::Allocator allocator);

    bool previous(cz::Allocator allocator);
    bool rfind_type(cz::Allocator allocator, Token_Type type);

    bool has_token() const;
    const Token& token() const;                         /// Note: asserts the token is valid.
    Contents_Iterator iterator_at_token_start() const;  /// Note: asserts the token is valid.

    Contents_Iterator check_point_iterator;
    cz::Vector<Token> tokens_since_check_point;

private:
    const Buffer* buffer_;
    Token token_;
};
}
