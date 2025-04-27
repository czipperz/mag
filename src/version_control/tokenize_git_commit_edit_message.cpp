#include "tokenize_git_commit_edit_message.hpp"

#include "contents.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "token.hpp"
#include "tokenize_patch.hpp"

namespace mag {
namespace syntax {

bool git_commit_edit_message_next_token(Contents_Iterator* iterator,
                                        Token* token,
                                        uint64_t* state) {
    // Show patch.
    if (*state == 2) {
        uint64_t s2 = (*state >> 2);
        bool result = patch_next_token(iterator, token, &s2);
        *state = ((*state & 3) | s2 << 2);
        return result;
    }

    if (iterator->at_eob()) {
        return false;
    }

    token->start = iterator->position;
    char ch = iterator->get();
    if (*state == 0 && ch == '#') {
        if (looking_at(*iterator, "# ------------------------ >8 ------------------------\n")) {
            *state = 2;
        }

        end_of_line(iterator);
        token->end = iterator->position;
        token->type = Token_Type::COMMENT;
        return true;
    }

    for (size_t i = 0;; ++i) {
        if (i >= 16 || iterator->at_eob()) {
            *state = 1;
            break;
        }
        if (iterator->get() == '\n') {
            iterator->advance();
            *state = 0;
            break;
        }
        iterator->advance();
    }
    token->end = iterator->position;
    token->type = Token_Type::DEFAULT;
    return true;
}

}
}
