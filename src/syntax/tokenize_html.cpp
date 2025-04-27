#include "tokenize_html.hpp"

#include <cz/char_type.hpp>
#include <tracy/Tracy.hpp>
#include "common.hpp"
#include "contents.hpp"
#include "face.hpp"
#include "match.hpp"
#include "token.hpp"
#include "tokenize_javascript.hpp"

namespace mag {
namespace syntax {

static bool islabelch(char ch) {
    // Every character except whitespace and a couple punctuation characters are allowed.
    return !(cz::Str("\t\n\f />\"'=").contains(ch));
}

enum Location : uint64_t {
    Default = 0,
    StartOfTag = 1,
    InAttributesList = 2,
    InAttributesListValue = 3,
};

bool html_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (*state >> 63) {
        if (js_next_token(iterator, token, state)) {
            return true;
        } else {
            if (looking_at(*iterator, "</")) {
                // Set only in_script_tag.
                *state = 4;
            }
        }
    }

    if (!advance_whitespace(iterator)) {
        return false;
    }

    token->start = iterator->position;
    Contents_Iterator start = *iterator;
    char first_ch = iterator->get();
    iterator->advance();

    // Where in the xml structure are we.
    uint64_t location = (*state & 3);
    // If inside a script tag.  IE the region `<script>` not the javascript
    // portion.  This is used because we need to save the state until the
    // `>` is reached because we can't backtrack to get the tag name.
    uint64_t in_script_tag = (*state & 4) != 0;

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
        location = StartOfTag;
        goto ret;
    } else if (first_ch == '>') {
        token->type = Token_Type::CLOSE_PAIR;
        location = Default;
        if (in_script_tag) {
            *state = ((uint64_t)1 << 63);
            token->end = iterator->position;
            return true;
        }
        goto ret;
    } else if (first_ch == '/' && !iterator->at_eob() && iterator->get() == '>') {
        iterator->advance();
        token->type = Token_Type::CLOSE_PAIR;
        location = Default;
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

    if (location == StartOfTag) {
        while (!iterator->at_eob()) {
            if (!islabelch(iterator->get())) {
                break;
            }
            iterator->advance();
        }

        if (matches(start, iterator->position, "script")) {
            in_script_tag = !in_script_tag;
        }

        token->type = Token_Type::HTML_TAG_NAME;
        location = InAttributesList;
        goto ret;
    }

    if (location == InAttributesList || location == InAttributesListValue) {
        if (islabelch(first_ch)) {
            while (!iterator->at_eob()) {
                if (!islabelch(iterator->get())) {
                    break;
                }
                iterator->advance();
            }

            if (location == InAttributesList)
                token->type = Token_Type::HTML_ATTRIBUTE_NAME;
            else
                token->type = Token_Type::STRING;
            goto ret;
        } else if (first_ch == '=') {
            token->type = Token_Type::PUNCTUATION;
            location = InAttributesListValue;
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
            location = InAttributesList;
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
    *state = (location | (in_script_tag << 2));

    token->end = iterator->position;
    return true;
}

}
}
