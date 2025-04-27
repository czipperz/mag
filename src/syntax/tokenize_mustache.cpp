#include "tokenize_general.hpp"

#include <cz/char_type.hpp>
#include <tracy/Tracy.hpp>
#include "common.hpp"
#include "core/contents.hpp"
#include "core/face.hpp"
#include "core/match.hpp"
#include "core/token.hpp"
#include "tokenize_general_hash_comments.hpp"

namespace mag {
namespace syntax {

namespace {
enum {
    START = 0,
    AT_ID = 1,
};
}

bool mustache_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (!advance_whitespace(iterator)) {
        return false;
    }

    token->start = iterator->position;

    cz::Str start_delim = "{{";
    cz::Str end_delim = "}}";

    if (looking_at(*iterator, start_delim)) {
        iterator->advance(start_delim.len);

        if (looking_at(*iterator, '!')) {
            token->type = Token_Type::COMMENT;
        } else if (looking_at(*iterator, '#') || looking_at(*iterator, '^') ||
                   looking_at(*iterator, '$') || looking_at(*iterator, '<') ||
                   looking_at(*iterator, '>')) {
            token->type = Token_Type::PREPROCESSOR_IF;
        } else if (looking_at(*iterator, '/')) {
            token->type = Token_Type::PREPROCESSOR_ENDIF;
        } else {
            token->type = Token_Type::OPEN_PAIR;
            *state = AT_ID;
            goto ret;
        }

        if (find(iterator, end_delim))
            iterator->advance(end_delim.len);
        goto ret;
    }

    if (*state != START && looking_at(*iterator, end_delim)) {
        iterator->advance(end_delim.len);
        *state = START;
        token->type = Token_Type::CLOSE_PAIR;
        goto ret;
    }

    return general_hash_comments_next_token(iterator, token, state);

ret:
    token->end = iterator->position;
    return true;
}

}
}
