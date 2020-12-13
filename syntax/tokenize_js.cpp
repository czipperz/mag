#include "tokenize_cpp.hpp"

#include <ctype.h>
#include "contents.hpp"
#include "face.hpp"
#include "match.hpp"
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
    return ch == '$' || ch == '_' || isalpha(ch);
}
static bool is_id_cont(char ch) {
    return is_id_start(ch) || isdigit(ch);
}

enum {
    BS_OPEN = 0,
    BS_INSIDE = 1,
    BS_CONTINUE = 2,
    BS_CURLY = 3,
};

static int32_t log2_u64(uint64_t v) {
    int32_t count = -1;
    while (v > 0) {
        v >>= 1;
        ++count;
    }
    return count;
}

static uint64_t fill_lower_bits(uint64_t v) {
    v |= (v >> 1);
    v |= (v >> 2);
    v |= (v >> 4);
    v |= (v >> 8);
    v |= (v >> 16);
    v |= (v >> 32);
    return v;
}

bool js_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    if (!advance_whitespace(iterator)) {
        return false;
    }

    // *state = aabbccdd1000...000e (little endian)
    // bs_depth = 4, bs_top = dd, expect_type = e

    bool expect_type = (*state & ((uint64_t)1 << 63)) >> 63;
    uint32_t bs_depth = (log2_u64(*state & 0x7FFFFFFFFFFFFFFF) + 1) / 2;
    int bs_top = -1;
    if (bs_depth > 0) {
        bs_top = ((*state & ((uint64_t)3 << ((bs_depth - 1) * 2))) >> ((bs_depth - 1) * 2));
    }

    token->start = iterator->position;

    Contents_Iterator start = *iterator;

    char first_ch = iterator->get();
    iterator->advance();

    if (bs_top == BS_OPEN) {
        bs_top = BS_INSIDE;
        if (first_ch == '$' && !iterator->at_eob() && iterator->get() == '{') {
            iterator->advance();
            token->type = Token_Type::OPEN_PAIR;
            goto ret;
        }
    }

    if (bs_top == BS_CONTINUE) {
        bs_depth--;
        bs_top = -1;
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
                bs_depth++;
                bs_top = BS_OPEN;
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
        first_ch == '%' || first_ch == '<' || first_ch == '>') {
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
        if (bs_depth > 0) {
            bs_depth++;
            bs_top = BS_CURLY;
        }
        goto ret;
    }

    if (first_ch == ')' || first_ch == ']') {
        token->type = Token_Type::CLOSE_PAIR;
        goto ret;
    }

    if (first_ch == '}') {
        token->type = Token_Type::CLOSE_PAIR;
        if (bs_depth > 0) {
            if (bs_top == BS_INSIDE) {
                bs_top = BS_CONTINUE;
            } else if (bs_top == BS_CURLY) {
                bs_depth--;
                bs_top = -1;
            }
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

        if (expect_type) {
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
                   matches(start, iterator->position, "undefined") ||
                   matches(start, iterator->position, "break") ||
                   matches(start, iterator->position, "continue") ||
                   matches(start, iterator->position, "false") ||
                   matches(start, iterator->position, "true") ||
                   matches(start, iterator->position, "of") ||
                   matches(start, iterator->position, "in") ||
                   matches(start, iterator->position, "static")) {
            token->type = Token_Type::KEYWORD;
        } else if (matches(start, iterator->position, "class") ||
                   matches(start, iterator->position, "new")) {
            token->type = Token_Type::KEYWORD;
            expect_type = true;
            goto ret2;
        } else {
            token->type = Token_Type::IDENTIFIER;
        }
        goto ret;
    }

    token->type = Token_Type::DEFAULT;

ret:
    expect_type = false;

ret2:
    token->end = iterator->position;

    uint64_t old_state = *state;
    *state = 0;
    if (expect_type) {
        *state |= ((uint64_t)1 << 63);
    }
    if (bs_depth > 0) {
        *state |= ((uint64_t)1 << (bs_depth * 2));
        *state |= (old_state & fill_lower_bits((uint64_t)1 << (bs_depth * 2 - 1)));
        if (bs_top != -1) {
            *state &= ~((uint64_t)3 << ((bs_depth - 1) * 2));
            *state |= ((uint64_t)bs_top << ((bs_depth - 1) * 2));
        }
    }
    return true;
}

}
}
