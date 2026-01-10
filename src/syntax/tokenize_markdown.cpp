#include "tokenize_markdown.hpp"

#include <cz/char_type.hpp>
#include <tracy/Tracy.hpp>
#include "core/contents.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"

namespace mag {
namespace syntax {

enum {
    START_OF_LINE,
    MIDDLE_OF_LINE,
    TITLE,
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
        if (*state == AFTER_LINK_TITLE) {
            *state = MIDDLE_OF_LINE;
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
    return ch == '`' || ch == '*' || ch == '_' || ch == '[' || cz::is_space(ch);
}

static bool advance_through_start_to_bold_or_italics_region(Contents_Iterator* iterator,
                                                            char first_ch) {
    char other_ch = (first_ch == '*' ? '_' : '*');

    // Eat the first char.
    iterator->advance();
    if (iterator->at_eob())
        return false;

    char second_ch = iterator->get();
    if (second_ch == first_ch || second_ch == other_ch) {
        iterator->advance();
        if (iterator->at_eob())
            return false;

        char third_ch = iterator->get();
        if (third_ch == other_ch) {
            iterator->advance();
            return true;  // length=3
        } else if (!is_special(third_ch)) {
            return true;  // length=2
        }
    } else if (!is_special(second_ch)) {
        return true;  // length=1
    }

    return false;
}

static bool at_word_boundary(Contents_Iterator iterator) {
    if (iterator.at_eob()) {
        return true;
    }
    char ch = iterator.get();
    if (ch == ')' || ch == ']' || ch == '}') {
        iterator.advance();
        if (iterator.at_eob()) {
            return true;
        }
        ch = iterator.get();
    }
    if (ch == ',' || ch == '.' || ch == ';' || ch == '?') {
        iterator.advance();
        if (iterator.at_eob()) {
            return true;
        }
        ch = iterator.get();

        if (ch == ')' || ch == ']' || ch == '}') {
            iterator.advance();
            if (iterator.at_eob()) {
                return true;
            }
            ch = iterator.get();
        }
    }
    return cz::is_space(ch);
}

bool find_end_of_bold_or_italics_region_this_line(Contents_Iterator* iterator,
                                                  Contents_Iterator start) {
    size_t pattern_length = iterator->position - start.position;
    char end_pattern[3];
    CZ_DEBUG_ASSERT(pattern_length <= sizeof(end_pattern));
    start.contents->slice_into(start, iterator->position, end_pattern);
    if (pattern_length >= 2) {
        std::swap(end_pattern[0], end_pattern[pattern_length - 1]);
    }
    return find_this_line(iterator, {end_pattern, pattern_length});
}

static bool md_next_token_helper(Contents_Iterator* iterator,
                                 Token* token,
                                 uint64_t* state,
                                 bool stop_at_hash_comment) {
    ZoneScoped;

    if (!advance_whitespace(iterator, state)) {
        return false;
    }

    token->start = iterator->position;
    char first_ch = iterator->get();

    if (*state == TITLE) {
        goto normal_character;
    }

    if (*state == START_OF_LINE && (first_ch == '*' || first_ch == '+' || first_ch == '-')) {
        iterator->advance();
        if (first_ch == '*' && !iterator->at_eob() && !cz::is_blank(iterator->get())) {
            // `\n*hello world*` should be parsed as italics not a list element.
            iterator->retreat();
        } else {
            token->type = Token_Type::PUNCTUATION;
            *state = MIDDLE_OF_LINE;
            goto ret;
        }
    }

    if (*state == START_OF_LINE && first_ch == '#') {
        if (stop_at_hash_comment)
            return false;
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

    if (first_ch == '(' || first_ch == '[' || first_ch == '{') {
        Contents_Iterator test = *iterator;
        test.advance();
        if (!test.at_eob()) {
            Contents_Iterator start = test;
            char second_ch = test.get();
            if (second_ch == '`') {
                *iterator = start;
                token->type = Token_Type::DEFAULT;
                goto ret;
            } else if ((second_ch == '*' || second_ch == '_') &&
                       advance_through_start_to_bold_or_italics_region(&test, second_ch)) {
                if (find_end_of_bold_or_italics_region_this_line(&test, start)) {
                    *iterator = start;
                    token->type = Token_Type::DEFAULT;
                    goto ret;
                }
            }
        }
    }

    if (first_ch == '*' || first_ch == '_') {
        // All possible combinations:
        // *   _   **   __   **_   *__   __*   _**

        Contents_Iterator start = *iterator;
        if (!advance_through_start_to_bold_or_italics_region(iterator, first_ch)) {
            *iterator = start;
            goto normal_character;
        }
        size_t pattern_length = iterator->position - start.position;

        if (!find_end_of_bold_or_italics_region_this_line(iterator, start)) {
            // Only capture bold/italics on same line to prevent it getting out of control.
            *iterator = start;
            goto normal_character;
        }
        iterator->advance(pattern_length);
        if (!at_word_boundary(*iterator)) {
            // Only capture if end is at word boundary.
            *iterator = start;
            goto normal_character;
        }

        if (pattern_length == 1)
            token->type = Token_Type::PROCESS_ITALICS;
        else if (pattern_length == 2)
            token->type = Token_Type::PROCESS_BOLD;
        else if (pattern_length == 3)
            token->type = Token_Type::PROCESS_BOLD_ITALICS;

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
        if (*state == TITLE) {
            if (cz::is_space(ch)) {
                break;
            }
        } else if (cz::is_space(ch)) {
            // Force at least one character in this token.
            if (token->start == iterator->position)
                iterator->advance();

            *state = MIDDLE_OF_LINE;
            break;
        }
        iterator->advance();
    }

ret:
    token->end = iterator->position;
    return true;
}

bool md_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    return md_next_token_helper(iterator, token, state, /*stop_at_hash_comment=*/false);
}

bool md_next_token_stop_at_hash_comment(Contents_Iterator* iterator,
                                        Token* token,
                                        uint64_t* state) {
    return md_next_token_helper(iterator, token, state, /*stop_at_hash_comment=*/true);
}

}
}
