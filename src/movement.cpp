#include "movement.hpp"

#include <ctype.h>
#include "buffer.hpp"
#include "contents.hpp"
#include "theme.hpp"
#include "token.hpp"

namespace mag {

void start_of_line(Contents_Iterator* iterator) {
    do {
        if (iterator->at_bob()) {
            return;
        }

        iterator->retreat();
    } while (iterator->get() != '\n');

    iterator->advance();
}

void end_of_line(Contents_Iterator* iterator) {
    while (!iterator->at_eob() && iterator->get() != '\n') {
        iterator->advance();
    }
}

void forward_through_whitespace(Contents_Iterator* iterator) {
    while (!iterator->at_eob() && isblank(iterator->get())) {
        iterator->advance();
    }
}

void backward_through_whitespace(Contents_Iterator* iterator) {
    do {
        if (iterator->at_bob()) {
            return;
        }

        iterator->retreat();
    } while (isblank(iterator->get()));

    iterator->advance();
}

void start_of_line_text(Contents_Iterator* iterator) {
    start_of_line(iterator);
    forward_through_whitespace(iterator);
}

uint64_t count_visual_columns(const Theme& theme,
                              Contents_Iterator iterator,
                              uint64_t end,
                              uint64_t column) {
    while (iterator.position < end) {
        if (iterator.get() == '\t') {
            column += theme.tab_width;
            column -= column % theme.tab_width;
        } else {
            column++;
        }
        iterator.advance();
    }

    return column;
}

uint64_t get_visual_column(const Theme& theme, Contents_Iterator iterator) {
    uint64_t end = iterator.position;
    start_of_line(&iterator);
    return count_visual_columns(theme, iterator, end);
}

void go_to_visual_column(const Theme& theme, Contents_Iterator* iterator, uint64_t column) {
    start_of_line(iterator);

    uint64_t current = 0;

    while (!iterator->at_eob() && current < column) {
        char ch = iterator->get();
        if (ch == '\n') {
            break;
        } else if (ch == '\t') {
            uint64_t col = current;
            col += theme.tab_width;
            col -= col % theme.tab_width;
            if (col > column) {
                if (col - column <= column - current) {
                    iterator->advance();
                }
                break;
            }
            current = col;
        } else {
            current++;
        }
        iterator->advance();
    }
}

void analyze_indent(const Theme& theme,
                    uint64_t columns,
                    uint64_t* num_tabs,
                    uint64_t* num_spaces) {
    if (theme.use_tabs) {
        *num_tabs = columns / theme.tab_width;
        *num_spaces = columns - *num_tabs * theme.tab_width;
    } else {
        *num_tabs = 0;
        *num_spaces = columns;
    }
}

void forward_line(const Theme& theme, Contents_Iterator* iterator) {
    uint64_t column = get_visual_column(theme, *iterator);

    Contents_Iterator backup = *iterator;
    end_of_line(iterator);

    if (iterator->at_eob()) {
        *iterator = backup;
        return;
    }

    iterator->advance();

    go_to_visual_column(theme, iterator, column);
}

void backward_line(const Theme& theme, Contents_Iterator* iterator) {
    uint64_t column = get_visual_column(theme, *iterator);

    Contents_Iterator backup = *iterator;
    start_of_line(iterator);

    if (iterator->at_bob()) {
        *iterator = backup;
        return;
    }

    iterator->retreat();

    go_to_visual_column(theme, iterator, column);
}

void forward_word(Contents_Iterator* iterator) {
    if (iterator->at_eob()) {
        return;
    }
    while (!iterator->at_eob() && !isalnum(iterator->get())) {
        iterator->advance();
    }
    while (!iterator->at_eob() && isalnum(iterator->get())) {
        iterator->advance();
    }
    return;
}

void backward_word(Contents_Iterator* iterator) {
    do {
        if (iterator->at_bob()) {
            return;
        }
        iterator->retreat();
    } while (!isalnum(iterator->get()));

    do {
        if (iterator->at_bob()) {
            return;
        }
        iterator->retreat();
    } while (isalnum(iterator->get()));

    iterator->advance();
}

void forward_char(Contents_Iterator* iterator) {
    if (!iterator->at_eob()) {
        iterator->advance();
    }
}

void backward_char(Contents_Iterator* iterator) {
    if (!iterator->at_bob()) {
        iterator->retreat();
    }
}

Contents_Iterator start_of_line_position(const Contents& contents, uint64_t lines) {
    Contents_Iterator iterator = contents.start();
    while (!iterator.at_eob() && lines > 1) {
        if (iterator.get() == '\n') {
            --lines;
        }
        iterator.advance();
    }
    return iterator;
}

bool get_token_at_position(Buffer* buffer, Contents_Iterator* token_iterator, Token* token) {
    uint64_t position = token_iterator->position;

    buffer->token_cache.update(buffer);
    Tokenizer_Check_Point check_point = {};
    buffer->token_cache.find_check_point(token_iterator->position, &check_point);

    token_iterator->retreat_to(check_point.position);
    uint64_t state = check_point.state;

    bool has_previous = false;
    Token previous_token;
    while (1) {
        bool has_token = buffer->mode.next_token(token_iterator, token, &state);
        if (has_previous) {
            auto low_priority = [](Token_Type type) {
                return type == Token_Type::OPEN_PAIR || type == Token_Type::CLOSE_PAIR ||
                       type == Token_Type::PUNCTUATION;
            };
            if (!has_token || token->start > position ||
                (low_priority(token->type) && !low_priority(previous_token.type))) {
                *token = previous_token;
                token_iterator->retreat_to(token->start);
                return true;
            }
        }

        if (!has_token || token->start > position) {
            return false;
        }

        if (token->end == position) {
            has_previous = true;
            previous_token = *token;
        }

        if (token->end > position) {
            token_iterator->retreat_to(token->start);
            return true;
        }
    }
}

}
