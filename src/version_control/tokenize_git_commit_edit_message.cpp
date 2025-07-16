#include "tokenize_git_commit_edit_message.hpp"

#include "core/contents.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"
#include "syntax/tokenize_markdown.hpp"
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

    char ch = iterator->get();
    if ((*state & 3) == State::AT_SOL && ch == '#') {
    sol:
        if (looking_at(*iterator, "# ------------------------ >8 ------------------------\n")) {
            *state = State::IN_PATCH;
        }

        token->start = iterator->position;
        end_of_line(iterator);
        token->end = iterator->position;
        token->type = Token_Type::COMMENT;
        return true;
    }

    uint64_t s2 = *state >> 2;
    bool has_next = md_next_token_stop_at_hash_comment(iterator, token, state);
    *state = ((*state & 3) | (s2 << 2));
    if (has_next || iterator->at_eob())
        return has_next;
    if (at_start_of_line(*iterator))
        goto sol;

    CZ_ASSERT(looking_at(*iterator, '#'));
    token->start = iterator->position;
    iterator->advance();
    token->end = iterator->position;
    token->type = Token_Type::DEFAULT;
    return true;
}

}
}
