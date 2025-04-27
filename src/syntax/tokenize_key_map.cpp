#include "tokenize_general.hpp"

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

enum {
    AT_KEY,
    AT_COMMAND,
};

bool key_map_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (!advance_whitespace(iterator)) {
        return false;
    }

    token->start = iterator->position;

    if (*state == AT_KEY) {
        // Eat everything except for the command word.
        end_of_line(iterator);
        rfind(iterator, ' ');

        // Retreat through all spaces.
        if (!iterator->at_bob()) {
            iterator->retreat();
            while (!iterator->at_bob()) {
                if (iterator->get() != ' ') {
                    break;
                }
                iterator->retreat();
            }
            iterator->advance();
        }

        token->type = Token_Type::SPLASH_KEY_BIND;
        *state = AT_COMMAND;
    } else {
        // Eat to the end of the line.
        end_of_line(iterator);

        token->type = Token_Type::IDENTIFIER;
        *state = AT_KEY;
    }

    token->end = iterator->position;
    return true;
}

}
}
