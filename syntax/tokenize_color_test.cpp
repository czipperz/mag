#include "tokenize_cpp.hpp"

#include <ctype.h>
#include "common.hpp"
#include "contents.hpp"
#include "face.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

static bool advance_whitespace(Contents_Iterator* iterator, bool* use_color) {
    while (1) {
        if (iterator->at_eob()) {
            return false;
        }

        char ch = iterator->get();
        if (ch == '\n') {
            *use_color = false;
        }
        if (!isspace(ch)) {
            return true;
        }
        iterator->advance();
    }
}

bool color_test_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    int16_t color = *state >> 1;
    bool use_color = *state & 1;

    if (!advance_whitespace(iterator, &use_color)) {
        return false;
    }

    token->start = iterator->position;

    if (use_color) {
        iterator->advance();
        token->type = Token_Type_::encode({{}, color, {}});
        ++color;
    } else {
        while (!iterator->at_eob()) {
            if (iterator->get() == '\n') {
                break;
            }
            if (iterator->get() == ':') {
                iterator->advance();
                use_color = true;
                break;
            }
            iterator->advance();
        }

        token->type = Token_Type::DEFAULT;
    }

    *state = ((color << 1) | use_color);

ret:
    token->end = iterator->position;
    return true;
}

}
}
