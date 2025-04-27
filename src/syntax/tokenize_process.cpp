#include "tokenize_process.hpp"

#include <tracy/Tracy.hpp>
#include "core/contents.hpp"
#include "core/token.hpp"

namespace mag {
namespace syntax {

enum : uint64_t {
    ITALICS_FLAG = 0x0000000000000001,
    BOLD_FLAG = 0x0000000000000002,
};

enum : char {
    BACKSPACE = 8,
};

bool process_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (iterator->at_eob()) {
        return false;
    }

    token->start = iterator->position;
    if (*state & (ITALICS_FLAG | BOLD_FLAG)) {
        // We are already in an escape sequence.  Test if the sequence continues or stops.
        char ch = iterator->get();
        iterator->advance();

        if (!iterator->at_eob() && iterator->get() == BACKSPACE) {
            // There are more escape sequence characters.
            if (ch == '_') {
                *state |= ITALICS_FLAG;
            } else {
                *state |= BOLD_FLAG;
            }
            iterator->advance();
            token->type = Token_Type::PROCESS_ESCAPE_SEQUENCE;
        } else {
            // The escape sequence is over
            if ((*state & ITALICS_FLAG) && (*state & BOLD_FLAG)) {
                token->type = Token_Type::PROCESS_BOLD_ITALICS;
            } else if (*state & ITALICS_FLAG) {
                token->type = Token_Type::PROCESS_ITALICS;
            } else {
                token->type = Token_Type::PROCESS_BOLD;
            }
            *state = 0;
        }

        token->end = iterator->position;
        return true;
    }

    // We aren't in an escape sequence.  Try to move forward until we hit one.
    token->type = Token_Type::DEFAULT;
    for (size_t clean_length = 0;; ++clean_length) {
        if (clean_length > 16) {
            // We didn't check that the 16th character is clean, but rather used it to check that
            // the previous one is.
            iterator->retreat();
            break;
        }

        if (iterator->get() == BACKSPACE) {
            if (clean_length > 1) {
                // Normal characters at start of token.
                iterator->retreat();
                break;
            } else {
                CZ_DEBUG_ASSERT(clean_length == 1);
                iterator->retreat();
                if (iterator->get() == '_') {
                    *state |= ITALICS_FLAG;
                } else {
                    *state |= BOLD_FLAG;
                }
                iterator->advance();
                iterator->advance();
                token->type = Token_Type::PROCESS_ESCAPE_SEQUENCE;
                break;
            }
        }

        iterator->advance();
        if (iterator->at_eob()) {
            break;
        }
    }

    token->end = iterator->position;
    return true;
}

}
}
