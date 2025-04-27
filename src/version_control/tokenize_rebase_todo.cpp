#include "tokenize_git_commit_edit_message.hpp"

#include "core/contents.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"

namespace mag {
namespace syntax {

bool git_rebase_todo_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    if (iterator->at_eob()) {
        return false;
    }

    token->start = iterator->position;
    char ch = iterator->get();
    if (ch == '#') {
        end_of_line(iterator);
        token->end = iterator->position;
        token->type = Token_Type::COMMENT;
        *state = 0;
        return true;
    }

    while (!iterator->at_eob()) {
        ch = iterator->get();
        if (ch == '#') {
            break;
        }
        if (ch == ' ' && *state < 2) {
            break;
        }
        if (ch == '\n') {
            iterator->advance();
            break;
        }
        iterator->advance();
    }

    token->end = iterator->position;
    if (ch == ' ' && *state < 2) {
        iterator->advance();
    }

    if (*state == 0) {
        token->type = Token_Type::GIT_REBASE_TODO_COMMAND;
        *state = 1;
    } else if (*state == 1) {
        token->type = Token_Type::GIT_REBASE_TODO_SHA;
        *state = 2;
    } else {
        token->type = Token_Type::GIT_REBASE_TODO_COMMIT_MESSAGE;
    }

    if (ch == '\n') {
        *state = 0;
    }
    return true;
}

}
}
