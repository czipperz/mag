#include "tokenize_css.hpp"

#include <cz/char_type.hpp>
#include <tracy/Tracy.hpp>
#include "common.hpp"
#include "contents.hpp"
#include "face.hpp"
#include "match.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

static bool is_id_start(char ch) {
    return ch == '-' || ch == '_' || cz::is_alpha(ch);
}
static bool is_id_cont(char ch) {
    return is_id_start(ch) || cz::is_digit(ch);
}

enum {
    TOP_LEVEL,
    AFTER_HASH,
    AFTER_DOT,
    AFTER_COLON,
    AFTER_NUMBER,
    BEFORE_PROPERTY,
    AFTER_PROPERTY,
    BEFORE_VALUE,
    BEFORE_COLOR_HEX,
};

uint8_t hex_value(char ch) {
    if (cz::is_digit(ch)) {
        return ch - '0';
    } else if (cz::is_upper(ch)) {
        return ch - 'A' + 10;
    } else {
        return ch - 'a' + 10;
    }
}

uint8_t double_hex_value(char ch) {
    uint8_t x = hex_value(ch);
    return x | (x << 4);
}

bool css_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (!advance_whitespace(iterator)) {
        return false;
    }

    token->start = iterator->position;

    char first_ch = iterator->get();
    iterator->advance();

    if (first_ch == '/' && !iterator->at_eob() && iterator->get() == '*') {
        // block comment
        if (find(iterator, "*/")) {
            iterator->advance(2);
        }

        token->type = Token_Type::COMMENT;
        goto ret;
    }

    if (*state == BEFORE_COLOR_HEX && cz::is_hex_digit(first_ch)) {
        Contents_Iterator end = *iterator;
        while (!end.at_eob() && cz::is_hex_digit(end.get())) {
            end.advance();
        }

        uint64_t diff = end.position - iterator->position + 1;
        Color color;
        if (diff == 3 || diff == 4) {
            color.r = double_hex_value(first_ch);

            color.g = double_hex_value(iterator->get());
            iterator->advance();

            color.b = double_hex_value(iterator->get());
            iterator->advance();

            for (uint64_t i = 3; i < diff; ++i) {
                // ignore the alpha value
                iterator->advance();
            }
        } else if (diff == 6 || diff == 8) {
            color.r = hex_value(first_ch) << 4;
            color.r |= hex_value(iterator->get());
            iterator->advance();

            color.g = hex_value(iterator->get()) << 4;
            iterator->advance();
            color.g |= hex_value(iterator->get());
            iterator->advance();

            color.b = hex_value(iterator->get()) << 4;
            iterator->advance();
            color.b |= hex_value(iterator->get());
            iterator->advance();

            for (uint64_t i = 6; i < diff; ++i) {
                // ignore the alpha value
                iterator->advance();
            }
        } else {
            goto not_a_color;
        }

        float luma = color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f;
        Color fg;
        if (luma > 120) {
            fg = {0x00, 0x00, 0x00};
        } else {
            fg = {0xFF, 0xFF, 0xFF};
        }

        token->type = Token_Type_::encode({fg, color, 0});
        *state = BEFORE_VALUE;
        goto ret;
    }
not_a_color:

    if (first_ch == '-' && *state == BEFORE_VALUE) {
        goto number_case;
    }

    if (is_id_start(first_ch)) {
        while (!iterator->at_eob() && is_id_cont(iterator->get())) {
            iterator->advance();
        }

        if (*state == BEFORE_PROPERTY) {
            *state = AFTER_PROPERTY;
            token->type = Token_Type::CSS_PROPERTY;
        } else if (*state == TOP_LEVEL) {
            *state = TOP_LEVEL;
            token->type = Token_Type::CSS_ELEMENT_SELECTOR;
        } else if (*state == AFTER_HASH) {
            *state = TOP_LEVEL;
            token->type = Token_Type::CSS_ID_SELECTOR;
        } else if (*state == AFTER_DOT) {
            *state = TOP_LEVEL;
            token->type = Token_Type::CSS_CLASS_SELECTOR;
        } else if (*state == AFTER_COLON) {
            *state = TOP_LEVEL;
            token->type = Token_Type::CSS_PSEUDO_SELECTOR;
        } else {
            token->type = Token_Type::IDENTIFIER;
        }

        goto ret;
    }

    if (first_ch == ':') {
        token->type = Token_Type::PUNCTUATION;
        if (*state == AFTER_PROPERTY) {
            *state = BEFORE_VALUE;
        } else if (*state == TOP_LEVEL) {
            *state = AFTER_COLON;
        }
        goto ret;
    }

    if (first_ch == ',') {
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }

    if (first_ch == '#') {
        token->type = Token_Type::PUNCTUATION;
        if (*state == BEFORE_VALUE) {
            *state = BEFORE_COLOR_HEX;
        } else {
            *state = AFTER_HASH;
        }
        goto ret;
    }

    if (first_ch == '.' && *state == TOP_LEVEL) {
        token->type = Token_Type::PUNCTUATION;
        *state = AFTER_DOT;
        goto ret;
    }

    if (cz::is_digit(first_ch) || first_ch == '.') {
        while (!iterator->at_eob() && (cz::is_digit(iterator->get()) || iterator->get() == '.')) {
        number_case:
            iterator->advance();
        }
        token->type = Token_Type::NUMBER;
        *state = AFTER_NUMBER;
        goto ret;
    }

    if (first_ch == ';') {
        token->type = Token_Type::PUNCTUATION;
        *state = BEFORE_PROPERTY;
        goto ret;
    }

    if (first_ch == '(' || first_ch == '{' || first_ch == '[') {
        token->type = Token_Type::OPEN_PAIR;
        if (first_ch == '{') {
            *state = BEFORE_PROPERTY;
        }
        goto ret;
    }

    if (first_ch == ')' || first_ch == '}' || first_ch == ']') {
        token->type = Token_Type::CLOSE_PAIR;
        if (first_ch == '}') {
            *state = TOP_LEVEL;
        }
        goto ret;
    }

    token->type = Token_Type::DEFAULT;

ret:
    token->end = iterator->position;
    return true;
}

}
}
