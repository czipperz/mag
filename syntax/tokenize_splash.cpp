#include "tokenize_general.hpp"

#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include "common.hpp"
#include "contents.hpp"
#include "face.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

bool splash_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (!advance_whitespace(iterator)) {
        return false;
    }

    token->start = iterator->position;
    char first_ch = iterator->get();
    token->type = Token_Type::DEFAULT;

    bool in_logo = iterator->position < start_of_line_position(*iterator->contents, 17).position;

    if (in_logo) {
        // Line 12 is the text in the logo.
        Contents_Iterator start_embedded_text = start_of_line_position(*iterator->contents, 12);

        Contents_Iterator end_embedded_text = start_embedded_text;
        find(&end_embedded_text, '.');
        forward_char(&end_embedded_text);

        bool in_embedded_text = iterator->position >= start_embedded_text.position &&
                                iterator->position < end_embedded_text.position;

        if (!in_embedded_text) {
            token->type = Token_Type::SPLASH_LOGO;
        }
    } else {
        // Look for C- and A-.
        bool hit = false;
        if (looking_at(*iterator, "C-")) {
            iterator->advance(2);
            hit = true;
        }
        if (looking_at(*iterator, "A-")) {
            iterator->advance(2);
            hit = true;
        }

        if (looking_at(*iterator, "F1")) {
            iterator->advance();
            hit = true;
        }

        if (hit) {
            // Eat the character (ie * in C-*), special casing SPACE.
            if (looking_at(*iterator, "SPACE")) {
                iterator->advance(5);
            } else {
                iterator->advance();
            }

            // If there is a second key include that too.
            Contents_Iterator it2 = *iterator;
            if (looking_at(it2, " ")) {
                it2.advance();

                if (!it2.at_eob() && it2.get() != ' ') {
                    // Ignore the next key.
                    it2.advance();

                    // Must be followed by a space.
                    if (looking_at(it2, " ")) {
                        *iterator = it2;
                    }
                }
            }

            token->type = Token_Type::SPLASH_KEY_BIND;
            goto ret;
        }

        if (first_ch == '`') {
            iterator->advance();
            if (find(iterator, '`')) {
                iterator->advance();
            }

            token->type = Token_Type::CODE;
            goto ret;
        }
    }

    iterator->advance();
    while (!iterator->at_eob()) {
        if (cz::is_space(iterator->get())) {
            break;
        }
        iterator->advance();
    }

ret:
    token->end = iterator->position;
    return true;
}

}
}
