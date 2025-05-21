#include "tokenize_search.hpp"

#include <cz/char_type.hpp>
#include <tracy/Tracy.hpp>
#include "common.hpp"
#include "core/contents.hpp"
#include "core/face.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"

namespace mag {
namespace syntax {

enum {
    FIRST_LINE,
    IN_CONTENTS,
};

bool build_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;
    if (iterator->at_eob()) {
        return false;
    }
    if (*state == FIRST_LINE) {
        token->type = Token_Type::SEARCH_COMMAND;
        token->start = iterator->position;
        end_of_line(iterator);
        token->end = iterator->position;
        *state = IN_CONTENTS;
        goto ret;
    }
    if (!advance_whitespace(iterator)) {
        return false;
    }

    token->type = Token_Type::DEFAULT;
    token->start = iterator->position;
    end_of_line(iterator);

ret:
    token->end = iterator->position;
    return true;
}

}
}
