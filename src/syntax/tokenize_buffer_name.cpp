#include "tokenize_path.hpp"

#include <cz/path.hpp>
#include <tracy/Tracy.hpp>
#include "core/contents.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"

namespace mag {
namespace syntax {

bool buffer_name_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (iterator->at_eob()) {
        return false;
    }

    // There are three name formats:
    // Name format 1: `/path/to/file`
    // Name format 2: `*temp name* (/path/to/directory/)`
    // Name format 3: `*temp name*`

    char first_ch = iterator->get();
    token->start = iterator->position;
    iterator->advance();

    // Handle cases 2/3.
    if (*state == 0 && first_ch == '*') {
        // Go either to end of name or after the `*` in `* (`.
        if (find(iterator, "* (")) {
            iterator->advance();
        }

        token->type = Token_Type::BUFFER_TEMPORARY_NAME;
        *state = 1;
        goto ret;
    }

    // Handle ( in case 2.
    if (*state == 1) {
        if (first_ch == ' ') {
            ++token->start;
            forward_char(iterator);
        }

        token->type = Token_Type::OPEN_PAIR;
        *state = 2;
        goto ret;
    }

    *state = 2;

    // Handle ) in case 2.
    if (first_ch == ')' && iterator->at_eob()) {
        token->type = Token_Type::CLOSE_PAIR;
        goto ret;
    }

    // Handle case 1 / path in case 2.
    if (cz::path::is_dir_sep(first_ch)) {
        token->type = Token_Type::PUNCTUATION;
    } else {
        token->type = Token_Type::DEFAULT;
        while (!iterator->at_eob() && !cz::path::is_dir_sep(iterator->get())) {
            iterator->advance();
        }
    }

ret:
    token->end = iterator->position;
    return true;
}

}
}
