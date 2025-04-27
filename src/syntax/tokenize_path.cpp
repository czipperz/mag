#include "tokenize_path.hpp"

#include <cz/path.hpp>
#include <tracy/Tracy.hpp>
#include "contents.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

bool path_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (iterator->at_eob()) {
        return false;
    }

    token->start = iterator->position;
    if (cz::path::is_dir_sep(iterator->get())) {
        token->type = Token_Type::PUNCTUATION;
        iterator->advance();
    } else {
        token->type = Token_Type::DEFAULT;
        iterator->advance();
        while (!iterator->at_eob() && !cz::path::is_dir_sep(iterator->get())) {
            iterator->advance();
        }
    }
    token->end = iterator->position;
    return true;
}

}
}
