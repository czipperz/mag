#include "token_movement_commands.hpp"

#include <algorithm>
#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "token.hpp"

namespace mag {
namespace basic {

static void tokenize_recording_pairs(Tokenizer_Check_Point check_point,
                                     Contents_Iterator token_iterator,
                                     uint64_t end_position,
                                     Buffer* buffer,
                                     cz::Vector<Token>* tokens) {
    Token token;
    token.end = check_point.position;
    uint64_t state = check_point.state;
    while (1) {
        bool has_token = buffer->mode.next_token(&token_iterator, &token, &state);
        if (has_token) {
            if (token.end > end_position) {
                break;
            }
            if (token.type == Token_Type::OPEN_PAIR || token.type == Token_Type::CLOSE_PAIR) {
                tokens->reserve(cz::heap_allocator(), 1);
                tokens->push(token);
            }
        } else {
            break;
        }
    }
}

void backward_up_pair(Buffer* buffer, Contents_Iterator* cursor) {
    uint64_t end_position = cursor->position;
    Contents_Iterator token_iterator = *cursor;

    Tokenizer_Check_Point check_point = {};
    buffer->token_cache.update(buffer);
    size_t check_point_index;
    if (buffer->token_cache.find_check_point(token_iterator.position, &check_point_index)) {
        check_point = buffer->token_cache.check_points[check_point_index];
    } else {
        check_point_index = 0;
    }

    cz::Vector<Token> tokens = {};
    tokens.reserve(cz::heap_allocator(), 8);
    CZ_DEFER(tokens.drop(cz::heap_allocator()));

    size_t depth = 0;
    while (1) {
        token_iterator.retreat_to(check_point.position);
        tokenize_recording_pairs(check_point, token_iterator, end_position, buffer, &tokens);

        for (size_t i = tokens.len(); i-- > 0;) {
            if (tokens[i].type == Token_Type::OPEN_PAIR) {
                if (depth == 0) {
                    cursor->retreat_to(tokens[i].start);
                    return;
                }

                --depth;
            } else {
                ++depth;
            }
        }

        if (check_point_index == 0) {
            return;
        }

        --check_point_index;
        end_position = check_point.position;
        check_point = buffer->token_cache.check_points[check_point_index];
        tokens.set_len(0);
    }
}

static bool get_token_after_position(Buffer* buffer,
                                     Contents_Iterator* token_iterator,
                                     uint64_t* state,
                                     Token* token) {
    uint64_t end_position = token_iterator->position;

    Tokenizer_Check_Point check_point = {};
    buffer->token_cache.update(buffer);
    buffer->token_cache.find_check_point(token_iterator->position, &check_point);
    token_iterator->retreat_to(check_point.position);
    *state = check_point.state;

    while (1) {
        bool has_token = buffer->mode.next_token(token_iterator, token, state);
        if (!has_token) {
            return false;
        }

        if (token->end > end_position) {
            return true;
        }
    }
}

static bool get_token_before_position(Buffer* buffer,
                                      Contents_Iterator* token_iterator,
                                      uint64_t* state,
                                      Token* token) {
    uint64_t end_position = token_iterator->position;

    Tokenizer_Check_Point check_point = {};
    buffer->token_cache.update(buffer);
    size_t check_point_index;
    if (buffer->token_cache.find_check_point(token_iterator->position, &check_point_index)) {
        check_point = buffer->token_cache.check_points[check_point_index];
    } else {
        check_point_index = 0;
    }

    while (1) {
        token_iterator->retreat_to(check_point.position);
        *state = check_point.state;

        bool has_token = buffer->mode.next_token(token_iterator, token, state);
        if (!has_token) {
            return false;
        }

        if (token->start >= end_position) {
            if (check_point_index == 0) {
                return false;
            }

            --check_point_index;
            end_position = check_point.position;
            check_point = buffer->token_cache.check_points[check_point_index];
            continue;
        }

        while (1) {
            Token previous_token = *token;
            bool has_token = buffer->mode.next_token(token_iterator, token, state);
            if (!has_token || token->start >= end_position) {
                *token = previous_token;
                return true;
            }
        }
    }
}

void forward_up_pair(Buffer* buffer, Contents_Iterator* cursor) {
    Contents_Iterator token_iterator = *cursor;

    Token token;
    uint64_t state;
    if (!get_token_after_position(buffer, &token_iterator, &state, &token)) {
        return;
    }

    size_t depth = 0;
    while (1) {
        if (token.type == Token_Type::CLOSE_PAIR) {
            if (depth == 0) {
                cursor->advance_to(token.end);
                return;
            }

            --depth;
        } else if (token.type == Token_Type::OPEN_PAIR) {
            ++depth;
        }

        bool has_token = buffer->mode.next_token(&token_iterator, &token, &state);
        if (!has_token) {
            return;
        }
    }
}

void command_backward_up_pair(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    for (size_t cursor_index = window->cursors.len(); cursor_index-- > 0;) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        backward_up_pair(buffer, &it);
        window->cursors[cursor_index].point = it.position;
    }
    std::sort(window->cursors.start(), window->cursors.end(),
              [](const Cursor& left, const Cursor& right) { return left.point < right.point; });
}

void command_forward_up_pair(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    for (size_t cursor_index = 0; cursor_index < window->cursors.len(); ++cursor_index) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        forward_up_pair(buffer, &it);
        window->cursors[cursor_index].point = it.position;
    }
    std::sort(window->cursors.start(), window->cursors.end(),
              [](const Cursor& left, const Cursor& right) { return left.point < right.point; });
}

void forward_token_pair(Buffer* buffer, Contents_Iterator* iterator) {
    Token token;
    uint64_t state;
    if (!get_token_after_position(buffer, iterator, &state, &token)) {
        return;
    }

    CZ_DEBUG_ASSERT(token.end == iterator->position);

    if (token.type == Token_Type::OPEN_PAIR) {
        forward_up_pair(buffer, iterator);
    }
}

void command_forward_token_pair(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    for (size_t cursor_index = 0; cursor_index < window->cursors.len(); ++cursor_index) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        forward_token_pair(buffer, &it);
        window->cursors[cursor_index].point = it.position;
    }
    std::sort(window->cursors.start(), window->cursors.end(),
              [](const Cursor& left, const Cursor& right) { return left.point < right.point; });
}

void backward_token_pair(Buffer* buffer, Contents_Iterator* iterator) {
    Token token;
    uint64_t state;
    if (!get_token_before_position(buffer, iterator, &state, &token)) {
        return;
    }

    iterator->retreat_to(token.start);

    if (token.type == Token_Type::CLOSE_PAIR) {
        backward_up_pair(buffer, iterator);
    }
}

void command_backward_token_pair(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    for (size_t cursor_index = 0; cursor_index < window->cursors.len(); ++cursor_index) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        backward_token_pair(buffer, &it);
        window->cursors[cursor_index].point = it.position;
    }

    std::sort(window->cursors.start(), window->cursors.end(),
              [](const Cursor& left, const Cursor& right) { return left.point < right.point; });
}

}
}
