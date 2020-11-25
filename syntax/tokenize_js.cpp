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

static bool matches_no_bounds_check(Contents_Iterator it, cz::Str query) {
    ZoneScoped;

    for (size_t i = 0; i < query.len; ++i) {
        if (it.get() != query[i]) {
            return false;
        }
        it.advance();
    }
    return true;
}

static bool matches(Contents_Iterator it, uint64_t end, cz::Str query) {
    ZoneScoped;

    if (end - it.position != query.len) {
        return false;
    }
    if (it.position + query.len > it.contents->len) {
        return false;
    }
    return matches_no_bounds_check(it, query);
}

static bool is_id_start(char ch) {
    return ch == '$' || ch == '_' || isalpha(ch);
}
static bool is_id_cont(char ch) {
    return is_id_start(ch) || isdigit(ch);
}

enum {
    DEFAULT_STATE,
    EXPECT_TYPE,

    BACKTICK_STRING_OPEN_VAR,
    INSIDE_BACKTICK_STRING,
    BACKTICK_STRING_CLOSE_VAR,
};

bool js_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    if (!advance_whitespace(iterator)) {
        return false;
    }

    token->start = iterator->position;

    Contents_Iterator start = *iterator;

    char first_ch = iterator->get();
    iterator->advance();

    if (*state == BACKTICK_STRING_OPEN_VAR) {
        *state = INSIDE_BACKTICK_STRING;
        if (first_ch == '$' && !iterator->at_eob() && iterator->get() == '{') {
            iterator->advance();
            token->type = Token_Type::OPEN_PAIR;
            goto ret;
        }
    }

    if (*state == BACKTICK_STRING_CLOSE_VAR) {
        *state = DEFAULT_STATE;
        first_ch = '`';
        iterator->retreat();
        goto inside_string;
    }

    if (first_ch == '\'' || first_ch == '"' || first_ch == '`') {
    inside_string:
        // strings
        while (!iterator->at_eob()) {
            char ch = iterator->get();
            if (ch == first_ch) {
                iterator->advance();
                break;
            }

            if (ch == '\\') {
                iterator->advance();
                if (iterator->at_eob()) {
                    break;
                }
            }

            iterator->advance();

            if (first_ch == '`' && ch == '$' && !iterator->at_eob() && iterator->get() == '{') {
                iterator->retreat();
                token->type = Token_Type::STRING;
                *state = BACKTICK_STRING_OPEN_VAR;
                goto ret;
            }
        }

        token->type = Token_Type::STRING;
        goto ret;
    }

    if (first_ch == '/' && !iterator->at_eob() && iterator->get() == '*') {
        // block comment
        char prev = 0;
        while (!iterator->at_eob()) {
            char ch = iterator->get();
            if (prev == '*' && ch == '/') {
                iterator->advance();
                break;
            }

            prev = ch;
            iterator->advance();
        }

        token->type = Token_Type::COMMENT;
        goto ret;
    }

    if (first_ch == '/' && !iterator->at_eob() && iterator->get() == '/') {
        // line comment
        while (!iterator->at_eob()) {
            if (iterator->get() == '\n') {
                iterator->advance();
                break;
            }

            iterator->advance();
        }

        token->type = Token_Type::COMMENT;
        goto ret;
    }

    if (first_ch == '.' || first_ch == ';' || first_ch == ',' || first_ch == '?' ||
        first_ch == ':') {
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }
    if (first_ch == '&' || first_ch == '|') {
        if (!iterator->at_eob() && iterator->get() == first_ch) {
            iterator->advance();
        }
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }
    if (first_ch == '=') {
        if (!iterator->at_eob() && iterator->get() == '=') {
            iterator->advance();
            if (!iterator->at_eob() && iterator->get() == '=') {
                iterator->advance();
            }
        }
        if (!iterator->at_eob() && iterator->get() == '>') {
            iterator->advance();
        }
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }
    if (first_ch == '!') {
        if (!iterator->at_eob() && iterator->get() == '=') {
            iterator->advance();
        }
        if (!iterator->at_eob() && iterator->get() == '=') {
            iterator->advance();
        }
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }
    if (first_ch == '+' || first_ch == '-' || first_ch == '*' || first_ch == '/' ||
        first_ch == '<' || first_ch == '>') {
        if (!iterator->at_eob() && iterator->get() == '=') {
            iterator->advance();
        }
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }

    if (first_ch == '(' || first_ch == '[') {
        token->type = Token_Type::OPEN_PAIR;
        goto ret;
    }

    if (first_ch == '{') {
        token->type = Token_Type::OPEN_PAIR;
        goto ret;
    }

    if (first_ch == ')' || first_ch == ']') {
        token->type = Token_Type::CLOSE_PAIR;
        goto ret;
    }

    if (first_ch == '}') {
        token->type = Token_Type::CLOSE_PAIR;
        if (*state == INSIDE_BACKTICK_STRING) {
            *state = BACKTICK_STRING_CLOSE_VAR;
        }
        goto ret;
    }

    if (is_id_start(first_ch)) {
        while (!iterator->at_eob()) {
            if (!is_id_cont(iterator->get())) {
                break;
            }
            iterator->advance();
        }

        if (*state == EXPECT_TYPE) {
            token->type = Token_Type::TYPE;
        } else if (matches(start, iterator->position, "const") ||
                   matches(start, iterator->position, "let") ||
                   matches(start, iterator->position, "var") ||
                   matches(start, iterator->position, "this") ||
                   matches(start, iterator->position, "function") ||
                   matches(start, iterator->position, "if") ||
                   matches(start, iterator->position, "else") ||
                   matches(start, iterator->position, "return") ||
                   matches(start, iterator->position, "for") ||
                   matches(start, iterator->position, "while") ||
                   matches(start, iterator->position, "constructor") ||
                   matches(start, iterator->position, "function") ||
                   matches(start, iterator->position, "async") ||
                   matches(start, iterator->position, "await") ||
                   matches(start, iterator->position, "null") ||
                   matches(start, iterator->position, "undefined")) {
            token->type = Token_Type::KEYWORD;
        } else if (matches(start, iterator->position, "class") ||
                   matches(start, iterator->position, "new")) {
            token->type = Token_Type::KEYWORD;
            *state = EXPECT_TYPE;
            goto ret2;
        } else {
            token->type = Token_Type::IDENTIFIER;
        }
        goto ret;
    }

    token->type = Token_Type::DEFAULT;

ret:
    if (*state == EXPECT_TYPE) {
        *state = DEFAULT_STATE;
    }

ret2:
    token->end = iterator->position;
    return true;
}

}
}
