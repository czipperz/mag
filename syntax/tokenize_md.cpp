#include "tokenize_md.hpp"

#include <ctype.h>
#include "contents.hpp"
#include "token.hpp"

namespace mag {
namespace custom {

static bool advance_whitespace(Contents_Iterator* iterator, uint64_t* state) {
    while (1) {
        if (iterator->at_eob()) {
            return false;
        }

        char ch = iterator->get();
        if (!isspace(ch)) {
            return true;
        }
        if (ch == '\n') {
            *state = 0;
        }
        iterator->advance();
    }
}

static void advance_through_multi_line_code_block(Contents_Iterator* iterator) {
    char first = 0;
    char second = 0;
    while (!iterator->at_eob()) {
        char third = iterator->get();
        iterator->advance();
        if (first == '`' && second == '`' && third == '`') {
            return;
        }
        first = second;
        second = third;
    }
}

static void advance_through_inline_code_block(Contents_Iterator* iterator) {
    while (!iterator->at_eob()) {
        char ch = iterator->get();
        iterator->advance();
        if (ch == '`') {
            return;
        }
    }
}

bool md_next_token(const Contents* contents,
                   Contents_Iterator* iterator,
                   Token* token,
                   uint64_t* state) {
    if (!advance_whitespace(iterator, state)) {
        return false;
    }

    token->start = iterator->position;
    char first_ch = iterator->get();

    if (*state == 0 && (first_ch == '*' || first_ch == '+' || first_ch == '-')) {
        iterator->advance();
        token->type = Token_Type::PUNCTUATION;
        *state = 1;
        goto ret;
    }

    if (*state == 0 && first_ch == '#') {
        iterator->advance();
        while (!iterator->at_eob() && iterator->get() == '#') {
            iterator->advance();
        }
        token->type = Token_Type::PUNCTUATION;
        *state = 2;
        goto ret;
    }

    if (first_ch == '`') {
        token->type = Token_Type::CODE;
        iterator->advance();
        if (iterator->at_eob()) {
            goto ret;
        }
        char second_ch = iterator->get();
        iterator->advance();
        if (iterator->at_eob()) {
            goto ret;
        }
        char third_ch = iterator->get();

        if (second_ch == '`' && third_ch == '`') {
            iterator->advance();
            advance_through_multi_line_code_block(iterator);
        } else if (second_ch == '`') {
            // Stop at this point
        } else {
            advance_through_inline_code_block(iterator);
        }

        goto ret;
    }

    while (!iterator->at_eob() && iterator->get() != '\n' && iterator->get() != '`') {
        iterator->advance();
    }
    if (*state == 2) {
        token->type = Token_Type::TITLE;
    } else {
        token->type = Token_Type::DEFAULT;
    }

ret:
    token->end = iterator->position;
    return true;
}

}
}
