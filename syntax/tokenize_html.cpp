#include "tokenize_html.hpp"

#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include "common.hpp"
#include "contents.hpp"
#include "face.hpp"
#include "match.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

static bool islabelch(char ch) {
    return cz::is_alnum(ch) || ch == '-' || ch == '_';
}

bool html_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (!advance_whitespace(iterator)) {
        return false;
    }

    token->start = iterator->position;

    char first_ch = iterator->get();
    iterator->advance();

    if (first_ch == '<') {
        if (!iterator->at_eob() && iterator->get() == '/') {
            iterator->advance();
        } else if (!iterator->at_eob() && iterator->get() == '!') {
            iterator->advance();

            Contents_Iterator it = *iterator;
            if (!it.at_eob() && it.get() == '-') {
                it.advance();
                if (!it.at_eob() && it.get() == '-') {
                    *iterator = it;

                    // block comment
                    if (find(iterator, "-->")) {
                        iterator->advance(3);
                    }

                    token->type = Token_Type::COMMENT;
                    goto ret;
                }
            }
        }
        token->type = Token_Type::OPEN_PAIR;
        *state = 1;
        goto ret;
    } else if (first_ch == '>') {
        token->type = Token_Type::CLOSE_PAIR;
        *state = 0;
        goto ret;
    } else if (first_ch == '/' && !iterator->at_eob() && iterator->get() == '>') {
        iterator->advance();
        token->type = Token_Type::CLOSE_PAIR;
        *state = 0;
        goto ret;
    } else if (first_ch == '&') {
        Contents_Iterator backup = *iterator;
        for (int i = 0;; ++i) {
            if (iterator->at_eob()) {
                break;
            }
            if (i == 16) {
                *iterator = backup;
                break;
            }
            if (iterator->get() == ';') {
                iterator->advance();
                break;
            }
            iterator->advance();
        }
        token->type = Token_Type::HTML_AMPERSAND_CODE;
        goto ret;
    }

    if (*state == 1) {
        while (!iterator->at_eob()) {
            if (!islabelch(iterator->get())) {
                break;
            }
            iterator->advance();
        }

        token->type = Token_Type::HTML_TAG_NAME;
        *state = 2;
        goto ret;
    }

    if (*state == 2) {
        if (islabelch(first_ch)) {
            while (!iterator->at_eob()) {
                if (!islabelch(iterator->get())) {
                    break;
                }
                iterator->advance();
            }

            token->type = Token_Type::HTML_ATTRIBUTE_NAME;
            goto ret;
        } else if (first_ch == '=') {
            token->type = Token_Type::PUNCTUATION;
            goto ret;
        } else if (first_ch == '"') {
            while (!iterator->at_eob()) {
                char ch = iterator->get();
                iterator->advance();

                if (ch == '\\') {
                    if (iterator->at_eob()) {
                        break;
                    }
                    iterator->advance();
                } else if (ch == '"') {
                    break;
                }
            }
            token->type = Token_Type::STRING;
            goto ret;
        }
    }

    while (!iterator->at_eob()) {
        char ch = iterator->get();
        if (cz::is_space(ch) || ch == '<' || ch == '>' || ch == '&') {
            break;
        }
        iterator->advance();
    }
    token->type = Token_Type::DEFAULT;

ret:
    token->end = iterator->position;
    return true;
}

}
}
