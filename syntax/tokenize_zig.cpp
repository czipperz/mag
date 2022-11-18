#include "tokenize_zig.hpp"

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

static bool is_id_start(char ch) {
    return ch == '_' || cz::is_alpha(ch) || ch == '@';
}
static bool is_id_cont(char ch) {
    return is_id_start(ch) || cz::is_digit(ch);
}

static bool is_number(Contents_Iterator it, uint64_t end) {
    if (it.get() != 'i' && it.get() != 'u')
        return false;
    it.advance();
    if (it.position == end)
        return false;
    while (it.position < end) {
        if (!cz::is_digit(it.get()))
            return false;
        it.advance();
    }
    return true;
}

bool zig_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

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

        if (matches(start, iterator->position, "pub") ||
            matches(start, iterator->position, "const") ||
            matches(start, iterator->position, "union") ||
            matches(start, iterator->position, "enum") ||
            matches(start, iterator->position, "struct") ||
            matches(start, iterator->position, "if") || matches(start, iterator->position, "var") ||
            matches(start, iterator->position, "while") ||
            matches(start, iterator->position, "true") ||
            matches(start, iterator->position, "false") ||
            matches(start, iterator->position, "return") ||
            matches(start, iterator->position, "error") ||
            matches(start, iterator->position, "test") ||
            matches(start, iterator->position, "try") ||
            matches(start, iterator->position, "switch") ||
            matches(start, iterator->position, "else") ||
            matches(start, iterator->position, "null") ||
            matches(start, iterator->position, "for") || matches(start, iterator->position, "fn")) {
            token->type = Token_Type::KEYWORD;
        } else if (matches(start, iterator->position, "usize") ||
                   matches(start, iterator->position, "isize") ||
                   is_number(start, iterator->position) ||
                   matches(start, iterator->position, "void")) {
            token->type = Token_Type::TYPE;
        } else {
            token->type = Token_Type::IDENTIFIER;
        }
        goto ret;
    }

    if (cz::is_digit(first_ch)) {
        while (!iterator->at_eob() && cz::is_alnum(iterator->get())) {
            iterator->advance();
        }

        token->type = Token_Type::NUMBER;
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
    if (first_ch == ':' || first_ch == '=' || first_ch == '!' || first_ch == '?' ||
        first_ch == '+' || first_ch == '-' || first_ch == '*' || first_ch == '/' ||
        first_ch == '%' || first_ch == '^' || first_ch == '|' || first_ch == '&') {
        if (!iterator->at_eob() && iterator->get() == '=') {
            iterator->advance();
        } else if (!iterator->at_eob() && iterator->get() == '/') {
            end_of_line(iterator);
            token->type = Token_Type::COMMENT;
            goto ret;
        } else if (!iterator->at_eob() && iterator->get() == '*') {
            find(iterator, "*/");
            token->type = Token_Type::COMMENT;
            goto ret;
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
