#include "tokenize_cmake.hpp"

#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include "common.hpp"
#include "contents.hpp"
#include "face.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

static bool is_special(char ch) {
    return ch == '"' || ch == '\'' || ch == '$' || ch == '{' || ch == '[' || ch == '(' ||
           ch == '}' || ch == ']' || ch == ')' || ch == '#' || cz::is_space(ch);
}

bool cmake_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (!advance_whitespace(iterator)) {
        return false;
    }

    token->start = iterator->position;
    char first_ch = iterator->get();
    iterator->advance();

    if (!is_special(first_ch)) {
        while (!iterator->at_eob() && !is_special(iterator->get())) {
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

    if (first_ch == '$') {
        token->type = Token_Type::PUNCTUATION;
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

    if (first_ch == '#') {
        end_of_line(iterator);
        token->type = Token_Type::COMMENT;
        goto ret;
    }

    token->type = Token_Type::DEFAULT;

ret:
    token->end = iterator->position;
    return true;
}

}
}
