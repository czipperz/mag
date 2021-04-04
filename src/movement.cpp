#include "movement.hpp"

#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include "buffer.hpp"
#include "contents.hpp"
#include "mode.hpp"
#include "token.hpp"

namespace mag {

void start_of_line(Contents_Iterator* iterator) {
    ZoneScoped;

    do {
        if (iterator->at_bob()) {
            return;
        }

        iterator->retreat();
    } while (iterator->get() != '\n');

    iterator->advance();
}

void end_of_line(Contents_Iterator* iterator) {
    ZoneScoped;
    while (!iterator->at_eob() && iterator->get() != '\n') {
        iterator->advance();
    }
}

void forward_through_whitespace(Contents_Iterator* iterator) {
    while (!iterator->at_eob() && cz::is_blank(iterator->get())) {
        iterator->advance();
    }
}

void backward_through_whitespace(Contents_Iterator* iterator) {
    do {
        if (iterator->at_bob()) {
            return;
        }

        iterator->retreat();
    } while (cz::is_blank(iterator->get()));

    iterator->advance();
}

void start_of_line_text(Contents_Iterator* iterator) {
    start_of_line(iterator);
    forward_through_whitespace(iterator);
}

uint64_t count_visual_columns(const Mode& mode,
                              Contents_Iterator iterator,
                              uint64_t end,
                              uint64_t column) {
    while (iterator.position < end) {
        if (iterator.get() == '\t') {
            column += mode.tab_width;
            column -= column % mode.tab_width;
        } else {
            column++;
        }
        iterator.advance();
    }

    return column;
}

uint64_t get_visual_column(const Mode& mode, Contents_Iterator iterator) {
    uint64_t end = iterator.position;
    start_of_line(&iterator);
    return count_visual_columns(mode, iterator, end);
}

void go_to_visual_column(const Mode& mode, Contents_Iterator* iterator, uint64_t column) {
    start_of_line(iterator);

    uint64_t current = 0;

    while (!iterator->at_eob() && current < column) {
        char ch = iterator->get();
        if (ch == '\n') {
            break;
        } else if (ch == '\t') {
            uint64_t col = current;
            col += mode.tab_width;
            col -= col % mode.tab_width;
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

void analyze_indent(const Mode& mode, uint64_t columns, uint64_t* num_tabs, uint64_t* num_spaces) {
    if (mode.use_tabs) {
        *num_tabs = columns / mode.tab_width;
        *num_spaces = columns - *num_tabs * mode.tab_width;
    } else {
        *num_tabs = 0;
        *num_spaces = columns;
    }
}

void forward_line(const Mode& mode, Contents_Iterator* iterator) {
    uint64_t column = get_visual_column(mode, *iterator);

    Contents_Iterator backup = *iterator;
    end_of_line(iterator);

    if (iterator->at_eob()) {
        *iterator = backup;
        return;
    }

    iterator->advance();

    go_to_visual_column(mode, iterator, column);
}

void backward_line(const Mode& mode, Contents_Iterator* iterator) {
    uint64_t column = get_visual_column(mode, *iterator);

    Contents_Iterator backup = *iterator;
    start_of_line(iterator);

    if (iterator->at_bob()) {
        *iterator = backup;
        return;
    }

    iterator->retreat();

    go_to_visual_column(mode, iterator, column);
}

void forward_word(Contents_Iterator* iterator) {
    if (iterator->at_eob()) {
        return;
    }
    while (!iterator->at_eob() && !cz::is_alnum(iterator->get())) {
        iterator->advance();
    }
    while (!iterator->at_eob() && cz::is_alnum(iterator->get())) {
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
    } while (!cz::is_alnum(iterator->get()));

    do {
        if (iterator->at_bob()) {
            return;
        }
        iterator->retreat();
    } while (cz::is_alnum(iterator->get()));

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

void forward_paragraph(Contents_Iterator* iterator) {
    start_of_line(iterator);
    if (iterator->at_eob()) {
        return;
    }

    if (iterator->get() == '\n') {
        while (1) {
            end_of_line(iterator);
            forward_char(iterator);
            if (iterator->at_eob() || iterator->get() != '\n') {
                break;
            }
        }
    }

    while (1) {
        end_of_line(iterator);
        forward_char(iterator);
        if (iterator->at_eob() || iterator->get() == '\n') {
            break;
        }
    }
}

void backward_paragraph(Contents_Iterator* iterator) {
    if (iterator->at_eob()) {
        backward_char(iterator);
    }
    start_of_line(iterator);
    if (iterator->at_eob()) {
        return;
    }

    if (iterator->get() == '\n') {
        while (1) {
            backward_char(iterator);
            start_of_line(iterator);
            if (iterator->at_bob() || iterator->get() != '\n') {
                break;
            }
        }
    }

    while (1) {
        backward_char(iterator);
        start_of_line(iterator);
        if (iterator->at_bob() || iterator->get() == '\n') {
            break;
        }
    }
}

Contents_Iterator start_of_line_position(const Contents& contents, uint64_t line) {
    ZoneScoped;

    if (line > 0) {
        --line;
    }

    Contents_Iterator it;
    it.contents = &contents;
    it.index = 0;

    uint64_t pos = 0;
    for (size_t i = 0; i < contents.buckets.len(); ++i) {
        if (line <= contents.bucket_lfs[i]) {
            it.position = pos;
            it.bucket = i;

            while (line > 0) {
                --line;
                end_of_line(&it);
                forward_char(&it);
            }

            return it;
        }

        line -= contents.bucket_lfs[i];
        pos += contents.buckets[i].len;
    }

    CZ_DEBUG_ASSERT(pos == contents.len);
    it.position = pos;
    it.bucket = contents.buckets.len();

    return it;
}

Contents_Iterator iterator_at_line_column(const Contents& contents,
                                          uint64_t line,
                                          uint64_t column) {
    Contents_Iterator iterator = start_of_line_position(contents, line);

    if (column > 0) {
        --column;
    }

    while (!iterator.at_eob()) {
        if (column == 0) {
            break;
        }
        if (iterator.get() == '\n') {
            break;
        }

        --column;
        iterator.advance();
    }

    return iterator;
}

bool get_token_at_position(Buffer* buffer, Contents_Iterator* token_iterator, Token* token) {
    buffer->token_cache.update(buffer);

    return get_token_at_position_no_update(buffer, token_iterator, token);
}

bool get_token_at_position_no_update(const Buffer* buffer,
                                     Contents_Iterator* token_iterator,
                                     Token* token) {
    ZoneScoped;

    uint64_t position = token_iterator->position;

    Tokenizer_Check_Point check_point = {};
    // Subtract one from the position so we can check if the previous
    // token is a better match if we happen to be between two tokens.
    buffer->token_cache.find_check_point(
        token_iterator->position > 0 ? token_iterator->position - 1 : 0, &check_point);

    token_iterator->retreat_to(check_point.position);
    uint64_t state = check_point.state;

    bool has_previous = false;
    Token previous_token;
    while (1) {
        bool has_token = buffer->mode.next_token(token_iterator, token, &state);
        if (has_previous) {
            auto low_priority = [](Token_Type type) {
                return type == Token_Type::OPEN_PAIR || type == Token_Type::CLOSE_PAIR ||
                       type == Token_Type::PUNCTUATION || type == Token_Type::DEFAULT;
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
