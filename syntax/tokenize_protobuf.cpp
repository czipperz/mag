#include "tokenize_protobuf.hpp"

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

static bool is_type_identifier(Contents_Iterator start, uint64_t end);

static bool is_id_start(char ch) {
    return ch == '_' || cz::is_alpha(ch);
}
static bool is_id_cont(char ch) {
    return is_id_start(ch) || cz::is_digit(ch);
}

namespace {
enum {
    DEFAULT = 0,
    AFTER_RPC = 1,
};
}

bool protobuf_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (!advance_whitespace(iterator)) {
        return false;
    }

    uint64_t new_state = DEFAULT;
    Contents_Iterator start = *iterator;
    token->start = iterator->position;
    char first_ch = iterator->get();
    iterator->advance();

    if (is_id_start(first_ch)) {
        while (!iterator->at_eob() && is_id_cont(iterator->get())) {
            iterator->advance();
        }

        if (matches(start, iterator->position, "enum") ||
            matches(start, iterator->position, "message") ||
            matches(start, iterator->position, "singular") ||
            matches(start, iterator->position, "optional") ||
            matches(start, iterator->position, "repeated") ||
            matches(start, iterator->position, "map") ||
            matches(start, iterator->position, "true") ||
            matches(start, iterator->position, "false") ||
            matches(start, iterator->position, "syntax") ||
            matches(start, iterator->position, "service") ||
            matches(start, iterator->position, "returns")) {
            token->type = Token_Type::KEYWORD;
        } else if (matches(start, iterator->position, "rpc")) {
            token->type = Token_Type::KEYWORD;
            new_state = AFTER_RPC;
        } else if (matches(start, iterator->position, "double") ||
                   matches(start, iterator->position, "float") ||
                   matches(start, iterator->position, "int32") ||
                   matches(start, iterator->position, "int64") ||
                   matches(start, iterator->position, "uint32") ||
                   matches(start, iterator->position, "uint64") ||
                   matches(start, iterator->position, "sint32") ||
                   matches(start, iterator->position, "sint64") ||
                   matches(start, iterator->position, "fixed32") ||
                   matches(start, iterator->position, "fixed64") ||
                   matches(start, iterator->position, "sfixed32") ||
                   matches(start, iterator->position, "sfixed64") ||
                   matches(start, iterator->position, "bool") ||
                   matches(start, iterator->position, "string") ||
                   matches(start, iterator->position, "bytes")) {
            token->type = Token_Type::TYPE;
        } else if (*state != AFTER_RPC && is_type_identifier(start, iterator->position)) {
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

    // Comments.
    if (first_ch == '/' && !iterator->at_eob()) {
        if (iterator->get() == '/') {
            end_of_line(iterator);
            token->type = Token_Type::COMMENT;
            goto ret;
        } else if (iterator->get() == '*') {
            iterator->advance();
            if (find(iterator, "*/"))
                iterator->advance(2);
            token->type = Token_Type::COMMENT;
            goto ret;
        }
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
    *state = new_state;
    token->end = iterator->position;
    return true;
}

static bool is_type_identifier(Contents_Iterator start, uint64_t end) {
    // First character must be upper case.
    if (cz::is_lower(start.get()))
        return false;

    bool any_lowercase = false;
    for (Contents_Iterator it = start; it.position < end; it.advance()) {
        char ch = it.get();
        if (ch == '_')
            return false;
        if (cz::is_lower(ch))
            any_lowercase = true;
    }

    return any_lowercase;
}

}
}
