#include "tokenize_cpp.hpp"

#include <ctype.h>
#include "contents.hpp"
#include "face.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

static bool advance_whitespace(Contents_Iterator* iterator) {
    while (1) {
        if (iterator->at_eob()) {
            return false;
        }

        char ch = iterator->get();
        if (!isspace(ch)) {
            return true;
        }
        iterator->advance();
    }
}

static bool is_id_start(char ch) {
    return ch == '_' || isalpha(ch);
}
static bool is_id_cont(char ch) {
    return is_id_start(ch) || isdigit(ch);
}

bool general_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    if (!advance_whitespace(iterator)) {
        return false;
    }

    Contents_Iterator start = *iterator;
    token->start = iterator->position;
    char first_ch = iterator->get();
    iterator->advance();

    if (is_id_start(first_ch)) {
        while (!iterator->at_eob() && is_id_cont(iterator->get())) {
            iterator->advance();
        }

        token->type = Token_Type::IDENTIFIER;
        goto ret;
    }

    if (first_ch == '"' || first_ch == '\'') {
        while (!iterator->at_eob()) {
            if (iterator->get() == first_ch) {
                iterator->advance();
                break;
            }
            if (iterator->get() == '\\') {
                iterator->advance();
                if (iterator->at_eob()) {
                    break;
                }
            }
            iterator->advance();
        }

        token->type = Token_Type::STRING;
        goto ret;
    }

    if (first_ch == '{' || first_ch == '[' || first_ch == '(') {
        token->type = Token_Type::OPEN_PAIR;
        goto ret;
    }
    if (first_ch == '}' || first_ch == ']' || first_ch == ')') {
        token->type = Token_Type::CLOSE_PAIR;
        goto ret;
    }

    if (first_ch == '<' || first_ch == '>') {
        if (!iterator->at_eob() && iterator->get() == first_ch) {
            iterator->advance();
        }
        if (!iterator->at_eob() && iterator->get() == '=') {
            iterator->advance();
        }
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }
    if (first_ch == ':' || first_ch == '=' || first_ch == '!' || first_ch == '+' ||
        first_ch == '-' || first_ch == '*' || first_ch == '/' || first_ch == '%' ||
        first_ch == '^' || first_ch == '|' || first_ch == '&') {
        if (!iterator->at_eob() && iterator->get() == '=') {
            iterator->advance();
        } else if ((first_ch == '+' || first_ch == '-' || first_ch == '|' || first_ch == '&') &&
                   !iterator->at_eob() && iterator->get() == first_ch) {
            iterator->advance();
        }
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }
    if (first_ch == ';' || first_ch == ',' || first_ch == '.') {
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }

    token->type = Token_Type::DEFAULT;

ret:
    token->end = iterator->position;
    return true;
}

}
}
