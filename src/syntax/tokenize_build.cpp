#include "tokenize_search.hpp"

#include <cz/char_type.hpp>
#include <tracy/Tracy.hpp>
#include "common.hpp"
#include "core/contents.hpp"
#include "core/face.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"

namespace mag {
namespace syntax {

enum {
    FIRST_LINE,
    IN_CONTENTS,
};

static bool try_eating_line_and_column_number(Contents_Iterator* iterator) {
    Contents_Iterator test = *iterator;

    for (int i = 0; i < 2; ++i) {
        if (!looking_at(test, ':'))
            return false;
        test.advance();
        if (test.at_eob())
            return false;
        if (!cz::is_digit(test.get()))
            return false;
        test.advance();
        while (!test.at_eob() && cz::is_digit(test.get()))
            test.advance();
    }

    *iterator = test;
    return true;
}

bool build_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;
    if (iterator->at_eob()) {
        return false;
    }
    if (*state == FIRST_LINE) {
        token->type = Token_Type::SEARCH_COMMAND;
        token->start = iterator->position;
        end_of_line(iterator);
        token->end = iterator->position;
        *state = IN_CONTENTS;
        goto ret;
    }
    if (!advance_whitespace(iterator)) {
        return false;
    }

    token->start = iterator->position;

#define IDENT_CASES \
    CZ_ALNUM_CASES: \
    case '.':       \
    case '/':       \
    case '_'
#define PUNCT_CASES                                                                              \
    '!' : case '#' : case '$' : case '%' : case '&' : case '*' : case '+' : case ',' : case '-'  \
        : case ':' : case ';' : case '<' : case '=' : case '>' : case '?' : case '@' : case '\\' \
        : case '^' : case '`' : case '|' : case '~' : case '"' : case '\''

    switch (char first_ch = iterator->get()) {
    case IDENT_CASES:
        token->type = Token_Type::IDENTIFIER;
        iterator->advance();
        for (; !iterator->at_eob(); iterator->advance()) {
            switch (iterator->get()) {
            case IDENT_CASES:
            case '-':
                continue;
            default:
                break;
            }
            break;
        }
        if (try_eating_line_and_column_number(iterator)) {
            token->type = Token_Type::LINK_HREF;
        }
        break;

    case '[':
    case '{':
    case '(':
        token->type = Token_Type::OPEN_PAIR;
        iterator->advance();
        break;
    case ']':
    case '}':
    case ')':
        token->type = Token_Type::CLOSE_PAIR;
        iterator->advance();
        break;

    case PUNCT_CASES:
        token->type = Token_Type::PUNCTUATION;
        iterator->advance();
        while (looking_at(*iterator, first_ch)) {
            iterator->advance();
        }
        break;

    default:
        token->type = Token_Type::DEFAULT;
        iterator->advance();
        break;
    }

ret:
    token->end = iterator->position;
    return true;
}

}
}
