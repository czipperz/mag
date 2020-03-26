#include "tokenize_path.hpp"

#include "contents.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

bool path_next_token(const Contents* contents,
                     Contents_Iterator* iterator,
                     Token* token,
                     uint64_t* state) {
    if (iterator->at_eob()) {
        return false;
    }

    token->start = iterator->position;
    if (iterator->get() == '/') {
        token->type = Token_Type::PUNCTUATION;
        iterator->advance();
    } else {
        token->type = Token_Type::DEFAULT;
        iterator->advance();
        while (!iterator->at_eob() && iterator->get() != '/') {
            iterator->advance();
        }
    }
    token->end = iterator->position;
    return true;
}

}
}
