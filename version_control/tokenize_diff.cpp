#include "tokenize_diff.hpp"

#include "contents.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

bool diff_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    if (iterator->at_eob()) {
        return false;
    }

    token->start = iterator->position;
    char ch = iterator->get();
    end_of_line(iterator);
    forward_char(iterator);
    token->end = iterator->position;
    switch (ch) {
    case '<':
        token->type = Token_Type::PATCH_REMOVE;
        break;
    case '>':
        token->type = Token_Type::PATCH_ADD;
        break;
    case '-':
        token->type = Token_Type::PATCH_NEUTRAL;
        break;
    case '\\':
        token->type = Token_Type::DEFAULT;
        break;
    default:
        token->type = Token_Type::PATCH_ANNOTATION;
        break;
    }
    return true;
}

}
}
