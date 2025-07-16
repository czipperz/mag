#include "tokenize_git_commit_edit_message.hpp"

#include "core/contents.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"
#include "tokenize_patch.hpp"

namespace mag {
namespace syntax {

namespace {
enum State : uint64_t {
    AT_SOL = 0,
    IN_MARKDOWN = 1,
    IN_PATCH = 2,
};
}

bool git_commit_edit_message_next_token(Contents_Iterator* iterator,
                                        Token* token,
                                        uint64_t* state) {
    // Show patch.
    if ((*state & 3) == State::IN_PATCH) {
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
    if ((*state & 3) == State::AT_SOL && ch == '#') {
        if (looking_at(*iterator, "# ------------------------ >8 ------------------------\n")) {
            *state = State::IN_PATCH;
        }

        end_of_line(iterator);
        token->end = iterator->position;
        token->type = Token_Type::COMMENT;
        return true;
    }

    for (size_t i = 0;; ++i) {
        if (i >= 16 || iterator->at_eob()) {
            *state = State::IN_MARKDOWN;
            break;
        }
        if (iterator->get() == '\n') {
            iterator->advance();
            *state = State::AT_SOL;
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
