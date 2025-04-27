#include "tokenize_color_test.hpp"

#include <cz/char_type.hpp>
#include <tracy/Tracy.hpp>
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
        if (!cz::is_space(ch)) {
            return true;
        }
        iterator->advance();
    }
}

bool color_test_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    int16_t color = (int16_t)(*state >> 1);
    bool use_color = *state & 1;

    if (!advance_whitespace(iterator, &use_color)) {
        return false;
    }

    token->start = iterator->position;

    if (use_color) {
        iterator->advance();
        token->type = Token_Type_::encode({color, color, {}});
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

    *state = (((uint64_t)color << 1) | (uint64_t)use_color);
    token->end = iterator->position;
    return true;
}

}
}
