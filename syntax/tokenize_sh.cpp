#include "tokenize_sh.hpp"

#include <cz/char_type.hpp>
#include "contents.hpp"
#include "face.hpp"
#include "match.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

enum : uint64_t {
    AT_START_OF_STATEMENT,
    NORMAL,
    AFTER_DOLLAR,
    IN_STRING,
};

static bool advance_whitespace(Contents_Iterator* iterator, uint64_t* top) {
    while (1) {
        if (iterator->at_eob()) {
            return false;
        }

        char ch = iterator->get();
        if (ch == '\n') {
            *top = AT_START_OF_STATEMENT;
        }
        if (!cz::is_space(ch)) {
            return true;
        }
        iterator->advance();
    }
}

static bool is_separator(char ch) {
    return ch == '?' || ch == '*' || ch == ';' || ch == '<' || ch == '|' || ch == '>' ||
           ch == '&' || ch == '{' || ch == '(' || ch == '[' || ch == '}' || ch == ')' ||
           ch == ']' || ch == '+';
}

static bool is_general(char ch) {
    return ch != '"' && ch != '\'' && ch != '`' && ch != '$' && ch != '#' && !is_separator(ch) &&
           !cz::is_space(ch);
}

bool sh_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    // We could use 5 bits for the depth but then if the
    // depth gets too large we risk corrupting the bitfield.
    uint64_t depth = *state >> 60;
    uint64_t prev = (*state >> (depth - 1) * 2) & 3;
    uint64_t top = (*state >> depth * 2) & 3;

#define PUSH(STATE)                            \
    do {                                       \
        *state &= ~((uint64_t)3 << depth * 2); \
        *state |= ((STATE) << depth * 2);      \
        depth += 1;                            \
    } while (0)

#define POP()                                   \
    do {                                        \
        CZ_DEBUG_ASSERT(depth > 0);             \
        depth -= 1;                             \
        prev = (*state >> (depth - 1) * 2) & 3; \
        top = (*state >> depth * 2) & 3;        \
    } while (0)

    if (!advance_whitespace(iterator, &top)) {
        return false;
    }

    token->start = iterator->position;

    Contents_Iterator start = *iterator;

    char first_ch = iterator->get();
    iterator->advance();

    if (top == IN_STRING || first_ch == '"') {
        token->type = Token_Type::STRING;

        if (top == IN_STRING && first_ch == '"') {
            top = NORMAL;
            goto ret;
        }

        top = NORMAL;

        while (!iterator->at_eob()) {
            char ch = iterator->get();
            if (ch == '"') {
                iterator->advance();
                break;
            }

            if (ch == '\\') {
                iterator->advance();
                if (iterator->at_eob()) {
                    break;
                }
            }

            if (ch == '$') {
                PUSH(IN_STRING);
                break;
            }

            iterator->advance();
        }

        goto ret;
    }

    if (first_ch == '{' || first_ch == '(' || first_ch == '[') {
        token->type = Token_Type::OPEN_PAIR;
        top = NORMAL;
        if (first_ch == '(') {
            top = AT_START_OF_STATEMENT;
        }
        goto ret;
    }
    if (first_ch == '}' || first_ch == ')' || first_ch == ']') {
        token->type = Token_Type::CLOSE_PAIR;
        top = NORMAL;
        if (depth >= 1 && prev == IN_STRING) {
            POP();
        }
        goto ret;
    }

    if (top == AFTER_DOLLAR &&
        ((is_separator(first_ch) && first_ch != ';') || first_ch == '$' || first_ch == '#')) {
        token->type = Token_Type::IDENTIFIER;
        top = NORMAL;
        if (depth >= 1 && prev == IN_STRING) {
            POP();
        }
        goto ret;
    }

    if (is_separator(first_ch)) {
        top = NORMAL;
        if (first_ch == ';' || first_ch == '|') {
            top = AT_START_OF_STATEMENT;
        }

        if (!iterator->at_eob()) {
            if ((first_ch == '&' || first_ch == '|') && iterator->get() == first_ch) {
                iterator->advance();
                top = AT_START_OF_STATEMENT;
            } else if (first_ch == '>' && iterator->get() == first_ch) {
                iterator->advance();
            } else if (first_ch == '>' && iterator->get() == '|') {
                iterator->advance();
            }
        }
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }

    if (top == AFTER_DOLLAR && (cz::is_alnum(first_ch) || first_ch == '_')) {
        while (!iterator->at_eob()) {
            if (!cz::is_alnum(iterator->get()) && iterator->get() != '_') {
                break;
            }
            iterator->advance();
        }
        token->type = Token_Type::IDENTIFIER;
        top = NORMAL;
        if (depth >= 1 && prev == IN_STRING) {
            POP();
        }
        goto ret;
    }

    if (is_general(first_ch)) {
        // Handle one-char variables specially like `$@` and `$*`.
        if (top == AFTER_DOLLAR) {
            token->type = Token_Type::IDENTIFIER;
            top = NORMAL;
            if (depth >= 1 && prev == IN_STRING) {
                POP();
            }
            goto ret;
        }

        if (first_ch == '\\' && !iterator->at_eob()) {
            iterator->advance();
        }
        while (!iterator->at_eob()) {
            char ch = iterator->get();
            if (ch == '\\') {
                iterator->advance();
                if (iterator->at_eob()) {
                    break;
                }
            } else {
                if (!is_general(ch)) {
                    break;
                }
                if (top == AT_START_OF_STATEMENT && ch == '=') {
                    token->type = Token_Type::IDENTIFIER;
                    top = NORMAL;
                    goto ret;
                }
            }
            iterator->advance();
        }

        if (top == AT_START_OF_STATEMENT && (matches(start, iterator->position, "if") ||
                                             matches(start, iterator->position, "elif") ||
                                             matches(start, iterator->position, "while") ||
                                             matches(start, iterator->position, "until") ||
                                             matches(start, iterator->position, "."))) {
            token->type = Token_Type::KEYWORD;
            top = AT_START_OF_STATEMENT;
            goto ret;
        } else if (top == AT_START_OF_STATEMENT && (matches(start, iterator->position, "then") ||
                                                    matches(start, iterator->position, "do") ||
                                                    matches(start, iterator->position, "case"))) {
            token->type = Token_Type::OPEN_PAIR;
            top = AT_START_OF_STATEMENT;
            goto ret;
        } else if (top == AT_START_OF_STATEMENT &&
                   (matches(start, iterator->position, "else") ||
                    matches(start, iterator->position, "for") ||
                    matches(start, iterator->position, "select") ||
                    matches(start, iterator->position, "continue") ||
                    matches(start, iterator->position, "break") ||
                    matches(start, iterator->position, "shift") ||
                    matches(start, iterator->position, "alias") ||
                    matches(start, iterator->position, "set") ||
                    matches(start, iterator->position, "unset") ||
                    matches(start, iterator->position, "cd") ||
                    matches(start, iterator->position, "mv") ||
                    matches(start, iterator->position, "cp") ||
                    matches(start, iterator->position, "test") ||
                    matches(start, iterator->position, "echo") ||
                    matches(start, iterator->position, "export"))) {
            token->type = Token_Type::KEYWORD;
        } else if (top == AT_START_OF_STATEMENT && (matches(start, iterator->position, "fi") ||
                                                    matches(start, iterator->position, "done") ||
                                                    matches(start, iterator->position, "esac"))) {
            token->type = Token_Type::CLOSE_PAIR;
        } else {
            token->type = Token_Type::DEFAULT;
        }
        top = NORMAL;
        goto ret;
    }

    if (first_ch == '\'') {
        while (!iterator->at_eob()) {
            if (iterator->get() == '\'') {
                iterator->advance();
                break;
            }
            iterator->advance();
        }
        token->type = Token_Type::STRING;
        top = NORMAL;
        goto ret;
    }

    if (first_ch == '#') {
        while (!iterator->at_eob()) {
            if (iterator->get() == '\n') {
                iterator->advance();
                break;
            }
            iterator->advance();
        }
        token->type = Token_Type::COMMENT;
        top = AT_START_OF_STATEMENT;
        goto ret;
    }

    if (first_ch == '$') {
        top = AFTER_DOLLAR;
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }

    token->type = Token_Type::DEFAULT;
    top = NORMAL;

ret:
    token->end = iterator->position;
    *state &= 0x0FFFFFFFFFFFFFFF;
    *state |= (depth << 60);
    *state &= ~((uint64_t)3 << depth * 2);
    *state |= (top << depth * 2);
    return true;
}

}
}
