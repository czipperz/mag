#include "token_iterator.hpp"

namespace mag {
void Forward_Token_Iterator::init_at_check_point(const Buffer* buffer, uint64_t position) {
    *this = {};
    tokenizer = buffer->mode.next_token;
    token_ = INVALID_TOKEN;

    Tokenizer_Check_Point check_point = buffer->token_cache.find_check_point(position);
    tokenization_iterator = buffer->contents.iterator_at(check_point.position);
    state = check_point.state;
}

bool Forward_Token_Iterator::init_at_or_after(const Buffer* buffer, uint64_t position) {
    init_at_check_point(buffer, position);
    return find_at_or_after(position);
}

bool Forward_Token_Iterator::init_after(const Buffer* buffer, uint64_t position) {
    init_at_check_point(buffer, position);
    return find_after(position);
}

bool Forward_Token_Iterator::next() {
    bool found = (*tokenizer)(&tokenization_iterator, &token_, &state);
#ifndef NDEBUG
    if (found) {
        token_.assert_valid(tokenization_iterator.contents->len);
    }
#endif
    if (!found) {
        token_ = INVALID_TOKEN;
    }
    return found;
}

bool Forward_Token_Iterator::find_at_or_after(uint64_t position) {
    if (has_token() && token_.end >= position) {
        return true;
    }
    while (next()) {
        if (token_.end >= position)
            return true;
    }
    return false;
}

bool Forward_Token_Iterator::find_after(uint64_t position) {
    if (!find_at_or_after(position)) {
        return false;
    }
    if (token_.start >= position) {
        return true;
    }
    return next();
}

bool Forward_Token_Iterator::find_type(Token_Type type) {
    if (has_token() && token_.type == type) {
        return true;
    }
    while (next()) {
        if (token_.type == type)
            return true;
    }
    return false;
}

bool Forward_Token_Iterator::has_token() const {
    return token_.is_valid(tokenization_iterator.contents->len);
}
const Token& Forward_Token_Iterator::token() const {
    token_.assert_valid(tokenization_iterator.contents->len);
    return token_;
}
Contents_Iterator Forward_Token_Iterator::iterator_at_token_start() const {
    token_.assert_valid(tokenization_iterator.contents->len);
    Contents_Iterator it = tokenization_iterator;
    it.retreat_to(token_.start);
    return it;
}

////////////////////////////////////////////////////////////////////////////////

void Backward_Token_Iterator::drop() {
    tokens_since_check_point.drop();
}

bool Backward_Token_Iterator::init_at_or_before(const Buffer* buffer, uint64_t position) {
    *this = {};
    buffer_ = buffer;
    token_ = INVALID_TOKEN;
    return cache_until(jump_to_check_point(position), position);
}

Forward_Token_Iterator Backward_Token_Iterator::jump_to_check_point(uint64_t position) {
    Forward_Token_Iterator it;
    it.init_at_check_point(buffer_, position);
    check_point_iterator = it.tokenization_iterator;
    return it;
}

bool Backward_Token_Iterator::cache_until(Forward_Token_Iterator it, uint64_t position) {
    while (it.next()) {
        const auto& token = it.token();
        tokens_since_check_point.reserve(1);
        tokens_since_check_point.push(token);
        if (token.end >= position) {
            return true;
        }
    }
    return false;
}

bool Backward_Token_Iterator::cache_previous_check_point() {
    return cache_until(jump_to_check_point(check_point_iterator.position - 1),
                       check_point_iterator.position);
}

bool Backward_Token_Iterator::previous() {
    while (1) {
        if (tokens_since_check_point.len != 0) {
            token_ = tokens_since_check_point.last();
            --tokens_since_check_point.len;
            return true;
        }

        if (check_point_iterator.position == 0) {
            token_ = INVALID_TOKEN;
            return false;
        }

        cache_previous_check_point();
    }
}

bool Backward_Token_Iterator::rfind_type(Token_Type type) {
    if (has_token() && token_.type == type) {
        return true;
    }
    while (previous()) {
        if (token_.type == type) {
            return true;
        }
    }
    return false;
}

bool Backward_Token_Iterator::has_token() const {
    return token_.is_valid(check_point_iterator.contents->len);
}
const Token& Backward_Token_Iterator::token() const {
    token_.assert_valid(check_point_iterator.contents->len);
    return token_;
}
Contents_Iterator Backward_Token_Iterator::iterator_at_token_start() const {
    token_.assert_valid(check_point_iterator.contents->len);
    Contents_Iterator it = check_point_iterator;
    it.advance_to(token_.start);
    return it;
}
}
