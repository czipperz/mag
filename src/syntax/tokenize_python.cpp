#include "tokenize_python.hpp"

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

static bool is_id_start(char ch) {
    return ch == '_' || cz::is_alpha(ch);
}
static bool is_id_cont(char ch) {
    return is_id_start(ch) || cz::is_digit(ch);
}

bool python_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
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

        if (matches(start, iterator->position, "and") || matches(start, iterator->position, "as") ||
            matches(start, iterator->position, "assert") ||
            matches(start, iterator->position, "break") ||
            matches(start, iterator->position, "class") ||
            matches(start, iterator->position, "continue") ||
            matches(start, iterator->position, "def") ||
            matches(start, iterator->position, "del") ||
            matches(start, iterator->position, "elif") ||
            matches(start, iterator->position, "else") ||
            matches(start, iterator->position, "except") ||
            matches(start, iterator->position, "False") ||
            matches(start, iterator->position, "finally") ||
            matches(start, iterator->position, "for") ||
            matches(start, iterator->position, "from") ||
            matches(start, iterator->position, "global") ||
            matches(start, iterator->position, "if") ||
            matches(start, iterator->position, "import") ||
            matches(start, iterator->position, "in") || matches(start, iterator->position, "is") ||
            matches(start, iterator->position, "lambda") ||
            matches(start, iterator->position, "None") ||
            matches(start, iterator->position, "nonlocal") ||
            matches(start, iterator->position, "not") || matches(start, iterator->position, "or") ||
            matches(start, iterator->position, "pass") ||
            matches(start, iterator->position, "raise") ||
            matches(start, iterator->position, "return") ||
            matches(start, iterator->position, "True") ||
            matches(start, iterator->position, "try") ||
            matches(start, iterator->position, "while") ||
            matches(start, iterator->position, "with") ||
            matches(start, iterator->position, "yield")) {
            token->type = Token_Type::KEYWORD;
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

    // Triple-delimiter strings
    if (first_ch == '"') {
        if (looking_at(*iterator, "\"\"")) {
            iterator->advance(2);
            if (find(iterator, "\"\"\""))
                iterator->advance(3);
            token->type = Token_Type::STRING;
            goto ret;
        }
    }
    if (first_ch == '\'') {
        if (looking_at(*iterator, "''")) {
            iterator->advance(2);
            if (find(iterator, "'''"))
                iterator->advance(3);
            token->type = Token_Type::STRING;
            goto ret;
        }
    }

    // Single-delimiter strings
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

    if (first_ch == '#') {
        // line comment
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
