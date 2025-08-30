#include "token_iterator.hpp"

namespace mag {
void Forward_Token_Iterator::init_at_check_point(const Buffer* buffer, uint64_t position) {
    *this = {};
    tokenizer = buffer->mode.next_token;
    token = INVALID_TOKEN;

    Tokenizer_Check_Point check_point = buffer->token_cache.find_check_point(position);
    iterator = buffer->contents.iterator_at(check_point.position);
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
    bool found = (*tokenizer)(&iterator, &token, &state);
#ifndef NDEBUG
    if (found) {
        token.assert_valid(iterator.contents->len);
    }
#endif
    if (!found) {
        token = INVALID_TOKEN;
    }
    return found;
}

bool Forward_Token_Iterator::find_at_or_after(uint64_t position) {
    if (token.is_valid(iterator.contents->len) && token.end >= position) {
        return true;
    }
    while (next()) {
        if (token.end >= position)
            return true;
    }
    return false;
}

bool Forward_Token_Iterator::find_after(uint64_t position) {
    if (!find_at_or_after(position)) {
        return false;
    }
    if (token.start >= position) {
        return true;
    }
    return next();
}

bool Forward_Token_Iterator::find_type(Token_Type type) {
    if (token.is_valid(iterator.contents->len) && token.type == type) {
        return true;
    }
    while (next()) {
        if (token.type == type)
            return true;
    }
    return false;
}
}
