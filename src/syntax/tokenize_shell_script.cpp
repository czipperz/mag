#include "tokenize_shell_script.hpp"

#include <cz/char_type.hpp>
#include <tracy/Tracy.hpp>
#include "core/contents.hpp"
#include "core/face.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"

namespace mag {
namespace syntax {

enum : uint64_t {
    AT_START_OF_STATEMENT,
    AFTER_EXPORT,
    NORMAL,
    IN_CURLY_VAR,
    IN_STRING,

    IN_BACKTICK_MASK = 8,
};

enum : uint64_t {
    TRANSIENT_NORMAL,
    TRANSIENT_AFTER_DOLLAR,
};

static bool advance_whitespace(Contents_Iterator* iterator, uint64_t* top) {
    while (1) {
        if (iterator->at_eob()) {
            return false;
        }

        char ch = iterator->get();
        if (ch == '\n' && *top != IN_STRING) {
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
           ch == ']' || ch == '+' || ch == '=';
}

static bool is_general(char ch) {
    return ch != '"' && ch != '\'' && ch != '`' && ch != '$' && ch != '#' && !is_separator(ch) &&
           !cz::is_space(ch);
}

bool sh_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    // 63-60    59-58       57-0
    // depth  transient  state stack
    uint64_t depth = *state >> 60;
    uint64_t transient = (*state >> 58) & 0x3;
    uint64_t prev = (*state >> (depth - 1) * 4) & 0x7;
    uint64_t top = (*state >> depth * 4) & 0xf;
    // The transient state is reset if not handled manually to prevent weird states.
    uint64_t new_transient = TRANSIENT_NORMAL;

#define PUSH()                                   \
    do {                                         \
        *state &= ~((uint64_t)0xf << depth * 4); \
        *state |= (top << depth * 4);            \
        depth += 1;                              \
        prev = top;                              \
    } while (0)

#define POP()                                     \
    do {                                          \
        CZ_DEBUG_ASSERT(depth > 0);               \
        depth -= 1;                               \
        top = prev;                               \
        prev = (*state >> (depth - 1) * 4) & 0xf; \
    } while (0)

    if (!advance_whitespace(iterator, &top)) {
        return false;
    }

    token->start = iterator->position;

    Contents_Iterator start = *iterator;

    char first_ch = iterator->get();

    if ((transient != TRANSIENT_AFTER_DOLLAR || first_ch == '"') &&
        ((top == IN_STRING || first_ch == '"') && (first_ch != '$' && first_ch != '`'))) {
        token->type = Token_Type::STRING;

        if (first_ch == '"') {
            iterator->advance();
        }

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
                top = IN_STRING;
                // Note: we are stopping before the $ so we don't need to set the transient.
                break;
            }

            if (ch == '`') {
                top = IN_STRING;
                // Note: '`' itself is handled separately; this is
                // just the part of the string leading up to the '`'.
                break;
            }

            iterator->advance();
        }

        goto ret;
    }

    iterator->advance();

    if (transient == TRANSIENT_AFTER_DOLLAR && first_ch == '{') {
        token->type = Token_Type::OPEN_PAIR;
        if (top == IN_STRING) {
            PUSH();
        }
        top = IN_CURLY_VAR;
        new_transient = TRANSIENT_AFTER_DOLLAR;
        goto ret;
    }
    if (top == IN_CURLY_VAR && first_ch == '}') {
        token->type = Token_Type::CLOSE_PAIR;
        top = NORMAL;
        if (depth >= 1 && prev == IN_STRING) {
            POP();
        }
        goto ret;
    }

    // Handle backtick by adding a mask to the previous state.
    if (first_ch == '`') {
        if (depth >= 1 && (prev & IN_BACKTICK_MASK) && top != IN_STRING) {
            token->type = Token_Type::CLOSE_PAIR;
            POP();
            top &= ~IN_BACKTICK_MASK;
            if (top == AT_START_OF_STATEMENT || top == AFTER_EXPORT) {
                top = NORMAL;
            }
        } else {
            token->type = Token_Type::OPEN_PAIR;
            top |= IN_BACKTICK_MASK;
            PUSH();
            top = AT_START_OF_STATEMENT;
        }
        goto ret;
    }

    if (first_ch == '{' || first_ch == '(' || first_ch == '[') {
        token->type = Token_Type::OPEN_PAIR;
        if (first_ch == '(') {
            PUSH();
            top = AT_START_OF_STATEMENT;
        } else if (top != IN_CURLY_VAR) {
            top = NORMAL;
        }
        goto ret;
    }
    if (first_ch == '}' || first_ch == ')' || first_ch == ']') {
        token->type = Token_Type::CLOSE_PAIR;
        if (top != IN_CURLY_VAR) {
            top = NORMAL;
        }
        if (first_ch == ')' && depth >= 1) {
            POP();
        }
        goto ret;
    }

    if (transient == TRANSIENT_AFTER_DOLLAR &&
        ((is_separator(first_ch) && first_ch != ';') || first_ch == '$' || first_ch == '#')) {
        token->type = Token_Type::IDENTIFIER;
        goto ret;
    }

    if (is_separator(first_ch) && top != IN_CURLY_VAR) {
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
            } else if (first_ch == '+' && iterator->get() == '=') {
                iterator->advance();
            }
        }
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }

    if (transient == TRANSIENT_AFTER_DOLLAR && (cz::is_alnum(first_ch) || first_ch == '_')) {
        while (!iterator->at_eob()) {
            if (!cz::is_alnum(iterator->get()) && iterator->get() != '_') {
                break;
            }
            iterator->advance();
        }
        token->type = Token_Type::IDENTIFIER;
        goto ret;
    }

    if (is_general(first_ch)) {
        // Handle one-char variables specially like `$@` and `$*`.
        if (transient == TRANSIENT_AFTER_DOLLAR && first_ch != '\\') {
            token->type = Token_Type::IDENTIFIER;
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
                if ((top == AT_START_OF_STATEMENT || top == AFTER_EXPORT) &&
                    (ch == '=' || looking_at(*iterator, "+="))) {
                assignment:
                    token->type = Token_Type::IDENTIFIER;
                    top = NORMAL;
                    goto ret;
                }
                if (!is_general(ch)) {
                    if (top == AFTER_EXPORT)
                        goto assignment;
                    break;
                }
            }
            iterator->advance();
        }

        if (top == AT_START_OF_STATEMENT && (matches(start, iterator->position, "if") ||
                                             matches(start, iterator->position, "while") ||
                                             matches(start, iterator->position, "until") ||
                                             matches(start, iterator->position, "case"))) {
            token->type = Token_Type::OPEN_PAIR;
            top = AT_START_OF_STATEMENT;
            goto ret;
        } else if (top == AT_START_OF_STATEMENT && (matches(start, iterator->position, "else") ||
                                                    matches(start, iterator->position, "elif"))) {
            token->type = Token_Type::DIVIDER_PAIR;
            top = AT_START_OF_STATEMENT;
            goto ret;
        } else if (top == AT_START_OF_STATEMENT && (matches(start, iterator->position, "fi") ||
                                                    matches(start, iterator->position, "done") ||
                                                    matches(start, iterator->position, "esac"))) {
            token->type = Token_Type::CLOSE_PAIR;
            top = AT_START_OF_STATEMENT;
            goto ret;
        } else if (top == AT_START_OF_STATEMENT && matches(start, iterator->position, "for")) {
            token->type = Token_Type::OPEN_PAIR;
            new_transient = TRANSIENT_AFTER_DOLLAR;
        } else if (top == AT_START_OF_STATEMENT &&
                   (matches(start, iterator->position, ".") ||
                    matches(start, iterator->position, "then") ||
                    matches(start, iterator->position,
                            "do") || matches(start, iterator->position, "in"))) {
            token->type = Token_Type::KEYWORD;
            top = AT_START_OF_STATEMENT;
            goto ret;
        } else if (top == AT_START_OF_STATEMENT &&
                   (matches(start, iterator->position, "select") ||
                    matches(start, iterator->position, "continue") ||
                    matches(start, iterator->position, "break") ||
                    matches(start, iterator->position, "shift") ||
                    matches(start, iterator->position, "alias") ||
                    matches(start, iterator->position, "set") ||
                    matches(start, iterator->position, "unset") ||
                    matches(start, iterator->position, "cd") ||
                    matches(start, iterator->position, "builtin") ||
                    matches(start, iterator->position, "mv") ||
                    matches(start, iterator->position, "cp") ||
                    matches(start, iterator->position, "test") ||
                    matches(start, iterator->position, "echo") ||
                    matches(start, iterator->position, "exit") ||
                    matches(start, iterator->position, "return"))) {
            token->type = Token_Type::KEYWORD;
        } else if (top == AT_START_OF_STATEMENT && (matches(start, iterator->position, "export") ||
                                                    matches(start, iterator->position, "local"))) {
            token->type = Token_Type::KEYWORD;
            top = AFTER_EXPORT;
            goto ret;
        } else if (top == AT_START_OF_STATEMENT && (matches(start, iterator->position, "elif"))) {
            token->type = Token_Type::CLOSE_PAIR;
            top = AT_START_OF_STATEMENT;
            goto ret;
        } else if (top == AT_START_OF_STATEMENT && (matches(start, iterator->position, "fi") ||
                                                    matches(start, iterator->position, "done") ||
                                                    matches(start, iterator->position, "esac"))) {
            token->type = Token_Type::CLOSE_PAIR;
        } else {
            token->type = Token_Type::DEFAULT;
        }

        if (top == AT_START_OF_STATEMENT || top == AFTER_EXPORT) {
            top = NORMAL;
        }

        goto ret;
    }

    if (first_ch == '\'') {
        if (find(iterator, '\'')) {
            iterator->advance();
        }
        token->type = Token_Type::STRING;
        top = NORMAL;
        goto ret;
    }

    if (first_ch == '#' && top != IN_CURLY_VAR) {
        // line comment
        end_of_line(iterator);
        token->type = Token_Type::COMMENT;
        top = AT_START_OF_STATEMENT;
        goto ret;
    }

    if (first_ch == '$' && top != IN_CURLY_VAR) {
        token->type = Token_Type::PUNCTUATION;
        new_transient = TRANSIENT_AFTER_DOLLAR;
        if (top == AT_START_OF_STATEMENT) {
            top = NORMAL;
        }
        goto ret;
    }

    token->type = Token_Type::DEFAULT;
    if (top != IN_CURLY_VAR) {
        top = NORMAL;
    }

ret:
    token->end = iterator->position;
    *state &= 0x0FFFFFFFFFFFFFFF;
    *state |= (depth << 60);
    *state &= 0xF3FFFFFFFFFFFFFF;
    *state |= (new_transient << 58);
    *state &= ~((uint64_t)0xf << depth * 4);
    *state |= (top << depth * 4);
    return true;
}

}
}
