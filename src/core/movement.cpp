#include "movement.hpp"

#include <cz/char_type.hpp>
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/contents.hpp"
#include "core/match.hpp"
#include "core/mode.hpp"
#include "core/token.hpp"
#include "core/window.hpp"

namespace mag {

void start_of_line(Contents_Iterator* iterator) {
    if (rfind(iterator, '\n')) {
        iterator->advance();
    }
}

void end_of_line(Contents_Iterator* iterator) {
    find(iterator, '\n');
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

void end_of_line_text(Contents_Iterator* iterator) {
    end_of_line(iterator);
    backward_through_whitespace(iterator);
}

void start_of_visual_line(const Window_Unified* window,
                          const Mode& mode,
                          const Theme& theme,
                          Contents_Iterator* iterator) {
    if (!mode.wrap_long_lines) {
        start_of_line(iterator);
        return;
    }

    size_t window_cols = window->total_cols - line_number_cols(theme, window, iterator->contents);

    uint64_t end = iterator->position;
    start_of_line(iterator);
    uint64_t column = count_visual_columns(mode, *iterator, end);
    go_to_visual_column(mode, iterator, column - (column % window_cols));
}

void end_of_visual_line(const Window_Unified* window,
                        const Mode& mode,
                        const Theme& theme,
                        Contents_Iterator* iterator) {
    if (!mode.wrap_long_lines) {
        end_of_line(iterator);
        return;
    }

    size_t window_cols = window->total_cols - line_number_cols(theme, window, iterator->contents);

    uint64_t end = iterator->position;
    start_of_line(iterator);
    uint64_t column = count_visual_columns(mode, *iterator, end);
    go_to_visual_column(mode, iterator, column - (column % window_cols) + (window_cols - 1));
}

void forward_visual_line(const Window_Unified* window,
                         const Mode& mode,
                         const Theme& theme,
                         Contents_Iterator* iterator,
                         uint64_t rows) {
    if (!mode.wrap_long_lines) {
        forward_line(mode, iterator, rows);
        return;
    }

    size_t window_cols = window->total_cols - line_number_cols(theme, window, iterator->contents);
    uint64_t cols = window_cols;

    Contents_Iterator start = *iterator;
    start_of_line(&start);
    Contents_Iterator end = *iterator;
    end_of_line(&end);

    uint64_t column = count_visual_columns(mode, start, iterator->position);
    uint64_t line_width = count_visual_columns(mode, *iterator, end.position, column);

    while (rows-- > 0) {
        // If we have to go to the next line and there is no next line then stop.
        if (column + cols > line_width && end.at_eob()) {
            break;
        }

        // Last row is shorter than `column` columns wide.  Stop at the end of this column.
        if (rows == 0 && column < line_width - line_width % cols && column + cols >= line_width) {
            *iterator = end;
            return;
        }

        column += cols;
        if (column > line_width) {
            column = column % cols;

            *iterator = end;
            iterator->advance();
            start = *iterator;
            end = *iterator;
            end_of_line(&end);
            line_width = count_visual_columns(mode, start, end.position);
        }
    }

    go_to_visual_column(mode, iterator, column);
}

void backward_visual_line(const Window_Unified* window,
                          const Mode& mode,
                          const Theme& theme,
                          Contents_Iterator* iterator,
                          uint64_t rows) {
    if (!mode.wrap_long_lines) {
        backward_line(mode, iterator, rows);
        return;
    }

    size_t window_cols = window->total_cols - line_number_cols(theme, window, iterator->contents);
    uint64_t cols = window_cols;

    Contents_Iterator start = *iterator;
    start_of_line(&start);

    uint64_t column = count_visual_columns(mode, start, iterator->position);

    while (rows-- > 0) {
        if (column >= cols) {
            column -= cols;
        } else {
            *iterator = start;
            if (iterator->at_bob()) {
                break;
            }

            iterator->retreat();

            start = *iterator;
            start_of_line(&start);

            uint64_t line_width = count_visual_columns(mode, start, iterator->position);
            column = line_width - line_width % cols + column % cols;
        }
    }

    go_to_visual_column(mode, iterator, column);
}

uint64_t char_visual_columns(const Mode& mode, char ch, uint64_t column) {
    if (ch == '\t') {
        column += mode.tab_width;
        column -= column % mode.tab_width;
    } else if (!cz::is_print(ch)) {
        char uch = ch;
        // We format non-printable characters as `\[%d;`.
        column += 3;
        if (uch >= 100) {
            column += 3;
        } else if (uch >= 10) {
            column += 2;
        } else {
            column += 1;
        }
    } else {
        column++;
    }
    return column;
}

uint64_t count_visual_columns(const Mode& mode,
                              Contents_Iterator iterator,
                              uint64_t end,
                              uint64_t column) {
    ZoneScoped;

    while (iterator.position < end) {
        column = char_visual_columns(mode, iterator.get(), column);
        iterator.advance();
    }

    return column;
}

uint64_t get_visual_column(const Mode& mode, Contents_Iterator iterator) {
    ZoneScoped;

    uint64_t end = iterator.position;
    start_of_line(&iterator);
    return count_visual_columns(mode, iterator, end);
}

void go_to_visual_column(const Mode& mode, Contents_Iterator* iterator, uint64_t column) {
    ZoneScoped;

    start_of_line(iterator);

    uint64_t current = 0;

    while (!iterator->at_eob() && current < column) {
        char ch = iterator->get();
        if (ch == '\n') {
            break;
        }

        uint64_t col = char_visual_columns(mode, ch, current);
        if (col > column)
            break;

        iterator->advance();
        current = col;
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

bool forward_line(const Mode& mode, Contents_Iterator* iterator, uint64_t lines) {
    uint64_t column = get_visual_column(mode, *iterator);

    for (uint64_t i = 0; i < lines; ++i) {
        Contents_Iterator backup = *iterator;
        end_of_line(iterator);

        if (iterator->at_eob()) {
            *iterator = backup;
            return false;
        }

        iterator->advance();
    }

    go_to_visual_column(mode, iterator, column);
    return true;
}

bool backward_line(const Mode& mode, Contents_Iterator* iterator, uint64_t lines) {
    uint64_t column = get_visual_column(mode, *iterator);

    bool first_line = false;
    for (uint64_t i = 0; i < lines; ++i) {
        start_of_line(iterator);

        if (iterator->at_bob()) {
            first_line = true;
            break;
        }

        iterator->retreat();
    }

    go_to_visual_column(mode, iterator, column);
    return first_line;
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

void forward_through_identifier(Contents_Iterator* iterator) {
    while (!iterator->at_eob()) {
        char ch = iterator->get();
        if (!cz::is_alnum(ch) && ch != '_') {
            break;
        }
        iterator->advance();
    }
}

void backward_through_identifier(Contents_Iterator* iterator) {
    while (!iterator->at_bob()) {
        iterator->retreat();
        char ch = iterator->get();
        if (!cz::is_alnum(ch) && ch != '_') {
            iterator->advance();
            break;
        }
    }
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
    backward_char(iterator);
    start_of_line(iterator);

    if (iterator->at_eob()) {
        CZ_DEBUG_ASSERT(iterator->contents->len == 0);
        return;
    }

    while (iterator->get() == '\n') {
        backward_char(iterator);
        start_of_line(iterator);
        if (iterator->at_bob()) {
            return;
        }
    }

    while (iterator->get() != '\n') {
        backward_char(iterator);
        start_of_line(iterator);
        if (iterator->at_bob()) {
            return;
        }
    }

    end_of_line(iterator);
    forward_char(iterator);
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
    for (size_t i = 0; i < contents.buckets.len; ++i) {
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
    it.bucket = contents.buckets.len;

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

bool get_token_at_position_contents(Buffer* buffer, uint64_t position, SSOStr* contents) {
    Contents_Iterator token_iterator = buffer->contents.iterator_at(position);
    Token token;
    if (!get_token_at_position(buffer, &token_iterator, &token)) {
        return false;
    }
    *contents = buffer->contents.slice(cz::heap_allocator(), token_iterator, token.end);
    return true;
}

bool get_token_at_position_no_update(const Buffer* buffer,
                                     Contents_Iterator* token_iterator,
                                     Token* token) {
    ZoneScoped;

    uint64_t position = token_iterator->position;

    // Subtract one from the position so we can check if the previous
    // token is a better match if we happen to be between two tokens.
    Tokenizer_Check_Point check_point = buffer->token_cache.find_check_point(
        token_iterator->position > 0 ? token_iterator->position - 1 : 0);

    token_iterator->retreat_to(check_point.position);
    uint64_t state = check_point.state;

    bool has_previous = false;
    Token previous_token;
    while (1) {
        bool has_token = buffer->mode.next_token(token_iterator, token, &state);
        if (has_previous) {
            auto priority = [](Token_Type type) {
                if (type == Token_Type::OPEN_PAIR || type == Token_Type::CLOSE_PAIR ||
                    type == Token_Type::DIVIDER_PAIR || type == Token_Type::PUNCTUATION) {
                    return 0;
                } else if (type == Token_Type::PREPROCESSOR_IF ||
                           type == Token_Type::PREPROCESSOR_ELSE ||
                           type == Token_Type::PREPROCESSOR_ENDIF) {
                    return 1;
                } else if (type == Token_Type::DEFAULT || type == Token_Type::COMMENT ||
                           type == Token_Type::DOC_COMMENT || type == Token_Type::STRING) {
                    return 2;
                } else {
                    return 3;
                }
            };
            if (!has_token || token->start > position ||
                priority(token->type) < priority(previous_token.type)) {
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

bool at_start_of_line(Contents_Iterator iterator) {
    return iterator.at_bob() || (iterator.retreat(), iterator.get() == '\n');
}

bool at_end_of_line(Contents_Iterator iterator) {
    return iterator.at_eob() || iterator.get() == '\n';
}

bool at_empty_line(Contents_Iterator iterator) {
    return at_start_of_line(iterator) && at_end_of_line(iterator);
}

}
