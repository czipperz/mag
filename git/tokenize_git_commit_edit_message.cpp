#include "tokenize_git_commit_edit_message.hpp"

#include "contents.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

bool git_commit_edit_message_next_token(const Contents* contents,
                                        Contents_Iterator* iterator,
                                        Token* token,
                                        uint64_t* state) {
    if (iterator->at_eob()) {
        return false;
    }

    token->start = iterator->position;
    char ch = iterator->get();
    if (ch == '#') {
        end_of_line(iterator);
        token->end = iterator->position;
        token->type = Token_Type::COMMENT;
        return true;
    }

    for (size_t i = 0; i < 16; ++i) {
        if (iterator->at_eob() || iterator->get() == '#') {
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
