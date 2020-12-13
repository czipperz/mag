#include "tokenize_cpp.hpp"

#include <ctype.h>
#include "contents.hpp"
#include "face.hpp"
#include "match.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

static bool advance_whitespace(Contents_Iterator* iterator, bool* at_start_of_statement) {
    while (1) {
        if (iterator->at_eob()) {
            return false;
        }

        char ch = iterator->get();
        if (ch == '\n') {
            *at_start_of_statement = true;
        }
        if (!isspace(ch)) {
            return true;
        }
        iterator->advance();
    }
}

static bool is_separator(char ch) {
    return ch == '?' || ch == '*' || ch == ';' || ch == '<' || ch == '|' || ch == '>' ||
           ch == '&' || ch == '{' || ch == '(' || ch == '[' || ch == '}' || ch == ')' || ch == ']';
}

static bool is_general(char ch) {
    return ch != '"' && ch != '\'' && ch != '`' && ch != '$' && ch != '#' && !is_separator(ch) &&
           !isspace(ch);
}

enum {
    NORMAL,
    AFTER_DOLLAR,
};

bool sh_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    bool at_start_of_statement = !(*state & ((uint64_t)1 << 63));
    int top = (*state & 3);

    if (!advance_whitespace(iterator, &at_start_of_statement)) {
        return false;
    }

    token->start = iterator->position;

    Contents_Iterator start = *iterator;

    char first_ch = iterator->get();
    iterator->advance();

    if (first_ch == '{' || first_ch == '(' || first_ch == '[') {
        token->type = Token_Type::OPEN_PAIR;
        at_start_of_statement = false;
        if (first_ch == '(') {
            at_start_of_statement = true;
        } else if (first_ch == '{') {
            goto ret2;
        }
        goto ret;
    }
    if (first_ch == '}' || first_ch == ')' || first_ch == ']') {
        token->type = Token_Type::CLOSE_PAIR;
        at_start_of_statement = false;
        goto ret;
    }

    if (top == AFTER_DOLLAR &&
        ((is_separator(first_ch) && first_ch != ';') || first_ch == '$' || first_ch == '#')) {
        token->type = Token_Type::IDENTIFIER;
        at_start_of_statement = first_ch == ';';
        goto ret;
    }

    if (is_separator(first_ch)) {
        at_start_of_statement = first_ch == ';' || first_ch == '|';
        if (!iterator->at_eob()) {
            if ((first_ch == '&' || first_ch == '|') && iterator->get() == first_ch) {
                iterator->advance();
                at_start_of_statement = true;
            } else if (first_ch == '>' && iterator->get() == first_ch) {
                iterator->advance();
            } else if (first_ch == '>' && iterator->get() == '|') {
                iterator->advance();
            }
        }
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }

    if (is_general(first_ch)) {
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
                if (at_start_of_statement && ch == '=') {
                    token->type = Token_Type::IDENTIFIER;
                    at_start_of_statement = false;
                    goto ret;
                }
            }
            iterator->advance();
        }

        if (top == AFTER_DOLLAR) {
            token->type = Token_Type::IDENTIFIER;
        } else if (at_start_of_statement && (matches(start, iterator->position, "if") ||
                                             matches(start, iterator->position, "elif") ||
                                             matches(start, iterator->position, "while") ||
                                             matches(start, iterator->position, "until") ||
                                             matches(start, iterator->position, "then") ||
                                             matches(start, iterator->position, "do") ||
                                             matches(start, iterator->position, "."))) {
            token->type = Token_Type::KEYWORD;
            at_start_of_statement = true;
            goto ret;
        } else if (at_start_of_statement && (matches(start, iterator->position, "else") ||
                                             matches(start, iterator->position, "fi") ||
                                             matches(start, iterator->position, "done") ||
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
        } else {
            token->type = Token_Type::DEFAULT;
        }
        at_start_of_statement = false;
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
        at_start_of_statement = false;
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
        at_start_of_statement = true;
        goto ret;
    }

    if (first_ch == '$') {
        top = AFTER_DOLLAR;
        token->type = Token_Type::PUNCTUATION;
        at_start_of_statement = false;
        goto ret2;
    }

    token->type = Token_Type::DEFAULT;

ret:
    top = NORMAL;

ret2:
    token->end = iterator->position;
    *state = 0;
    if (!at_start_of_statement) {
        *state |= ((uint64_t)1 << 63);
    }
    *state |= (uint64_t)top;
    return true;
}

}
}
