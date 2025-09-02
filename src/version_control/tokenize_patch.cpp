#include "tokenize_patch.hpp"

#include "core/contents.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"

namespace mag {
namespace syntax {

namespace {
enum State : uint64_t {
    IN_DIFF = 0,
    IN_COMMIT_MESSAGE = 1,
};
}

bool patch_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    while (1) {
        if (iterator->at_eob())
            return false;
        if (iterator->get() == '\n')
            iterator->advance();
        else
            break;
    }

    token->start = iterator->position;
    switch (iterator->get()) {
    case '-':
        token->type = Token_Type::PATCH_REMOVE;
        break;
    case '+':
        token->type = Token_Type::PATCH_ADD;
        break;
    case ' ':
        token->type =
            (*state == IN_COMMIT_MESSAGE ? Token_Type::DEFAULT : Token_Type::PATCH_NEUTRAL);
        break;
    case '@':
        token->type = Token_Type::PATCH_ANNOTATION;
        break;
    case '#':
        token->type = Token_Type::COMMENT;
        break;
    case 'c':
        if (looking_at(*iterator, "commit ")) {
            // If this is git show/git log then it'll show the commit
            // message which we would like to format as normal text.
            *state = IN_COMMIT_MESSAGE;
            token->type = Token_Type::PATCH_COMMIT_CONTEXT;
        } else {
            token->type = Token_Type::DEFAULT;
        }
        break;
    case 'd':
    case 'i':
        token->type = Token_Type::DEFAULT;
        if (looking_at(*iterator, "diff ")|| looking_at(*iterator, "index ")) {
            *state = IN_DIFF;
            token->type = Token_Type::PATCH_FILE_CONTEXT;
        }
        break;
    default:
        if (*state == IN_COMMIT_MESSAGE)
            token->type = Token_Type::PATCH_COMMIT_CONTEXT;
        else
            token->type = Token_Type::DEFAULT;
        break;
    }
    end_of_line(iterator);
    token->end = iterator->position;
    return true;
}

}
}
