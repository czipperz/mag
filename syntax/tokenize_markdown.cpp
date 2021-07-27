#include "tokenize_markdown.hpp"

#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include "contents.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

enum {
    START_OF_LINE,
    MIDDLE_OF_LINE,
    TITLE,
    INSIDE_ITALICS,
    INSIDE_BOLD,
    INSIDE_BOLD_ITALICS,
    AFTER_LINK_TITLE,
    BEFORE_LINK_HREF_LINE,
    BEFORE_LINK_HREF_PAREN,
};

static bool advance_whitespace(Contents_Iterator* iterator, uint64_t* state) {
    while (1) {
        if (iterator->at_eob()) {
            return false;
        }

        char ch = iterator->get();
        if (!cz::is_space(ch)) {
            return true;
        }
        if (ch == '\n') {
            *state = START_OF_LINE;
        }
        iterator->advance();
    }
}

static void advance_through_multi_line_code_block(Contents_Iterator* iterator) {
    char first = 0;
    char second = 0;
    while (!iterator->at_eob()) {
        char third = iterator->get();
        iterator->advance();
        if (first == '`' && second == '`' && third == '`') {
            return;
        }
        first = second;
        second = third;
    }
}

static void advance_through_inline_code_block(Contents_Iterator* iterator) {
    while (!iterator->at_eob()) {
        char ch = iterator->get();
        iterator->advance();
        if (ch == '`') {
            return;
        }
    }
}

static bool is_special(char ch) {
    return ch == '`' || ch == '*' || ch == '_' || ch == '[';
}

bool md_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (!advance_whitespace(iterator, state)) {
        return false;
    }

    token->start = iterator->position;
    char first_ch = iterator->get();

    if (*state == START_OF_LINE && (first_ch == '*' || first_ch == '+' || first_ch == '-')) {
        iterator->advance();
        token->type = Token_Type::PUNCTUATION;
        *state = MIDDLE_OF_LINE;
        goto ret;
    }

    if (*state == START_OF_LINE && first_ch == '#') {
        iterator->advance();
        while (!iterator->at_eob() && iterator->get() == '#') {
            iterator->advance();
        }
        token->type = Token_Type::PUNCTUATION;
        *state = TITLE;
        goto ret;
    }

    if (first_ch == '`') {
        token->type = Token_Type::CODE;
        iterator->advance();
        if (iterator->at_eob()) {
            goto ret;
        }
        char second_ch = iterator->get();
        iterator->advance();
        if (iterator->at_eob()) {
            goto ret;
        }
        char third_ch = iterator->get();

        if (second_ch == '`' && third_ch == '`') {
            iterator->advance();
            advance_through_multi_line_code_block(iterator);
        } else if (second_ch == '`') {
            // Stop at this point
        } else {
            advance_through_inline_code_block(iterator);
        }

        goto ret;
    }

    if (first_ch == '[') {
        // Link
        token->type = Token_Type::LINK_TITLE;
        if (find(iterator, ']')) {
            iterator->advance();
        }

        *state = AFTER_LINK_TITLE;
        goto ret;
    }

    // [Title]: Href
    if (*state == AFTER_LINK_TITLE && looking_at(*iterator, ": ")) {
        iterator->advance();
        token->type = Token_Type::DEFAULT;
        *state = BEFORE_LINK_HREF_LINE;
        goto ret;
    }
    if (*state == BEFORE_LINK_HREF_LINE) {
        end_of_line(iterator);
        token->type = Token_Type::LINK_HREF;
        *state = START_OF_LINE;
        goto ret;
    }

    // [Title](Href)
    if (*state == AFTER_LINK_TITLE && first_ch == '(') {
        iterator->advance();
        token->type = Token_Type::DEFAULT;
        *state = BEFORE_LINK_HREF_PAREN;
        goto ret;
    }
    if (*state == BEFORE_LINK_HREF_PAREN) {
        find(iterator, ')');
        token->type = Token_Type::LINK_HREF;
        *state = MIDDLE_OF_LINE;
        goto ret;
    }

    if (*state == INSIDE_ITALICS || *state == INSIDE_BOLD || *state == INSIDE_BOLD_ITALICS ||
        first_ch == '*' || first_ch == '_') {
        bool italic = false;
        bool bold = false;

        auto assign_bold_and_italic = [&]() {
            iterator->advance();
            if (!iterator->at_eob() && (iterator->get() == '*' || iterator->get() == '_')) {
                iterator->advance();
                if (!iterator->at_eob() && (iterator->get() == '*' || iterator->get() == '_')) {
                    iterator->advance();
                    italic = !italic;
                }
                bold = !bold;
            } else {
                italic = !italic;
            }
        };

        if (*state == INSIDE_ITALICS) {
            italic = true;
        } else if (*state == INSIDE_BOLD) {
            bold = true;
        } else if (*state == INSIDE_BOLD_ITALICS) {
            bold = true;
            italic = true;
        } else {
            Contents_Iterator backup = *iterator;
            backup.retreat_to(token->start);
            backup.advance();

            assign_bold_and_italic();

            Contents_Iterator backup2 = *iterator;

            bool bold_backup = bold;
            bool italic_backup = italic;

            // Check that we have a paired * or ** in this line.
            while (1) {
                if (iterator->at_eob()) {
                    *iterator = backup;
                    goto normal_character;
                }

                char ch = iterator->get();
                if (ch == '\n') {
                    break;
                }

                if (!is_special(ch)) {
                    iterator->advance();
                    continue;
                }

                if (ch != '*' && ch != '_') {
                    *iterator = backup;
                    goto normal_character;
                }

                assign_bold_and_italic();
                if (!bold && !italic) {
                    break;
                }
                iterator->advance();
            }

            if (!bold && !italic) {
                bold = bold_backup;
                italic = italic_backup;
                *iterator = backup2;
            } else {
                *iterator = backup;
                goto normal_character;
            }
        }

        while (!iterator->at_eob()) {
            if (is_special(iterator->get())) {
                break;
            }
            iterator->advance();
        }

        if (bold) {
            if (italic) {
                token->type = Token_Type::PROCESS_BOLD_ITALICS;
            } else {
                token->type = Token_Type::PROCESS_BOLD;
            }
        } else {
            if (italic) {
                token->type = Token_Type::PROCESS_ITALICS;
            } else {
                CZ_PANIC("unreachable");
            }
        }

        if (!iterator->at_eob() && (iterator->get() == '*' || iterator->get() == '_')) {
            assign_bold_and_italic();
        }

        if (bold) {
            if (italic) {
                *state = INSIDE_BOLD_ITALICS;
            } else {
                *state = INSIDE_BOLD;
            }
        } else {
            if (italic) {
                *state = INSIDE_ITALICS;
            } else {
                *state = MIDDLE_OF_LINE;
            }
        }

        goto ret;
    }

normal_character:
    if (*state == TITLE) {
        token->type = Token_Type::TITLE;
    } else {
        token->type = Token_Type::DEFAULT;
    }

    while (!iterator->at_eob()) {
        char ch = iterator->get();
        if (ch == '\n') {
            *state = START_OF_LINE;
            break;
        }
        if (is_special(ch)) {
            *state = MIDDLE_OF_LINE;
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
