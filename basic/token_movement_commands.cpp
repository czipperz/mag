#include "token_movement_commands.hpp"

#include <algorithm>
#include <cz/sort.hpp>
#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "match.hpp"
#include "movement.hpp"
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

static void sort_cursors(cz::Slice<Cursor> cursors) {
    cz::sort(cursors,
             [](const Cursor* left, const Cursor* right) { return left->point < right->point; });
}

void command_backward_up_pair(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    for (size_t cursor_index = window->cursors.len(); cursor_index-- > 0;) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        backward_up_pair(buffer, &it);
        window->cursors[cursor_index].point = it.position;
    }

    sort_cursors(window->cursors);
}

void command_forward_up_pair(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    for (size_t cursor_index = 0; cursor_index < window->cursors.len(); ++cursor_index) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        forward_up_pair(buffer, &it);
        window->cursors[cursor_index].point = it.position;
    }

    sort_cursors(window->cursors);
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

    sort_cursors(window->cursors);
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

    sort_cursors(window->cursors);
}

int find_backward_matching_token(Buffer* buffer,
                                 Contents_Iterator iterator,
                                 Token* this_token,
                                 Token* matching_token) {
    Contents_Iterator token_to_match_iterator = iterator;
    Token token_to_match;
    if (!get_token_at_position(buffer, &token_to_match_iterator, &token_to_match)) {
        return -1;
    }
    *this_token = token_to_match;

    uint64_t end_position = token_to_match.start;
    Contents_Iterator token_iterator = iterator;
    Token token;
    Tokenizer_Check_Point check_point = {};
    buffer->token_cache.update(buffer);
    size_t check_point_index;
    if (buffer->token_cache.find_check_point(token_iterator.position, &check_point_index)) {
        check_point = buffer->token_cache.check_points[check_point_index];
    } else {
        check_point_index = 0;
    }

    token_iterator.retreat_to(check_point.position);
    uint64_t state = check_point.state;

    bool found_token_this_loop = false;

    while (1) {
        bool has_token = buffer->mode.next_token(&token_iterator, &token, &state);
        if (!has_token) {
            return 0;
        }

        if (token.start >= end_position) {
            if (found_token_this_loop) {
                return 1;
            }
            if (check_point_index == 0) {
                return 0;
            }

            --check_point_index;
            end_position = check_point.position;
            check_point = buffer->token_cache.check_points[check_point_index];

            token_iterator.retreat_to(check_point.position);
            state = check_point.state;
            continue;
        }

        Contents_Iterator test_it = token_iterator;
        test_it.retreat_to(token.start);
        if (token_to_match.type == token.type &&
            matches(token_to_match_iterator, token_to_match.end, test_it, token.end)) {
            found_token_this_loop = true;
            *matching_token = token;
        }
    }
}

int backward_matching_token(Buffer* buffer, Contents_Iterator* iterator) {
    Token this_token;
    Token matching_token;
    int created = find_backward_matching_token(buffer, *iterator, &this_token, &matching_token);
    if (created != 1) {
        return created;
    }

    iterator->retreat_to(matching_token.start + (iterator->position - this_token.start));
    return 1;
}

void command_backward_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    for (size_t cursor_index = 0; cursor_index < window->cursors.len(); ++cursor_index) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        backward_matching_token(buffer, &it);
        window->cursors[cursor_index].point = it.position;
    }

    sort_cursors(window->cursors);
}

int find_forward_matching_token(Buffer* buffer,
                                Contents_Iterator iterator,
                                Token* this_token,
                                Token* matching_token) {
    Contents_Iterator token_to_match_iterator = iterator;
    Token token_to_match;
    if (!get_token_at_position(buffer, &token_to_match_iterator, &token_to_match)) {
        return -1;
    }
    *this_token = token_to_match;

    uint64_t end_position = token_to_match.end;

    Contents_Iterator token_iterator = iterator;
    Tokenizer_Check_Point check_point = {};
    buffer->token_cache.update(buffer);
    buffer->token_cache.find_check_point(token_iterator.position, &check_point);
    token_iterator.retreat_to(check_point.position);
    uint64_t state = check_point.state;

    while (1) {
        Token token;
        bool has_token = buffer->mode.next_token(&token_iterator, &token, &state);
        if (!has_token) {
            return 0;
        }

        if (token.end > end_position) {
            Contents_Iterator test_it = token_iterator;
            test_it.retreat_to(token.start);
            if (token_to_match.type == token.type &&
                matches(token_to_match_iterator, token_to_match.end, test_it, token.end)) {
                *matching_token = token;
                return 1;
            }
        }
    }
}

int forward_matching_token(Buffer* buffer, Contents_Iterator* iterator) {
    Token this_token;
    Token matching_token;
    int created = find_forward_matching_token(buffer, *iterator, &this_token, &matching_token);
    if (created != 1) {
        return created;
    }

    iterator->advance_to(matching_token.start + (iterator->position - this_token.start));
    return 1;
}

void command_forward_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    for (size_t cursor_index = 0; cursor_index < window->cursors.len(); ++cursor_index) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        forward_matching_token(buffer, &it);
        window->cursors[cursor_index].point = it.position;
    }

    sort_cursors(window->cursors);
}

static Cursor create_cursor_with_offsets(Cursor cursor,
                                         Contents_Iterator it) {
    Cursor new_cursor = {};
    new_cursor.point = it.position;
    if (cursor.mark < cursor.point) {
        if (it.position < cursor.point - cursor.mark) {
            new_cursor.mark = 0;
        } else {
            new_cursor.mark = new_cursor.point - (cursor.point - cursor.mark);
        }
    } else {
        if (it.position + (cursor.mark - cursor.point) > it.contents->len) {
            new_cursor.mark = it.contents->len;
        } else {
            new_cursor.mark = new_cursor.point + (cursor.mark - cursor.point);
        }
    }
    return new_cursor;
}

static int create_cursor_forward_matching_token(Buffer* buffer, Window_Unified* window) {
    Cursor cursor = window->cursors[window->cursors.len() - 1];
    Contents_Iterator it = buffer->contents.iterator_at(cursor.point);
    int created = forward_matching_token(buffer, &it);
    if (created != 1) {
        return created;
    }

    Cursor new_cursor = create_cursor_with_offsets(cursor, it);

    window->cursors.reserve(cz::heap_allocator(), 1);
    window->cursors.push(new_cursor);
    return 1;
}

static int create_cursor_backward_matching_token(Buffer* buffer, Window_Unified* window) {
    Cursor cursor = window->cursors[0];
    Contents_Iterator it = buffer->contents.iterator_at(cursor.point);
    int created = backward_matching_token(buffer, &it);
    if (created != 1) {
        return created;
    }

    Cursor new_cursor = create_cursor_with_offsets(cursor, it);

    window->cursors.reserve(cz::heap_allocator(), 1);
    window->cursors.insert(0, new_cursor);
    return 1;
}

static void show_no_create_cursor_message(Client* client) {
    client->show_message("No more cursors to create");
}

static void show_no_matching_token_message(Client* client) {
    client->show_message("Cursor is not positioned at a token");
}

static void show_created_messages(Client* client, int created) {
    if (created == -1) {
        show_no_matching_token_message(client);
    }
    if (created == 0) {
        show_no_create_cursor_message(client);
    }
}

void command_create_cursor_forward_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_forward_matching_token(buffer, window);
    show_created_messages(source.client, created);
}

void command_create_cursor_backward_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_matching_token(buffer, window);
    show_created_messages(source.client, created);
}

void command_create_cursors_to_end_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_forward_matching_token(buffer, window);
    if (created == 1) {
        while (create_cursor_forward_matching_token(buffer, window) == 1) {
        }
    }
    show_created_messages(source.client, created);
}

void command_create_cursors_to_start_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_matching_token(buffer, window);
    if (created == 1) {
        while (create_cursor_backward_matching_token(buffer, window) == 1) {
        }
    }
    show_created_messages(source.client, created);
}

void command_create_all_cursors_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_matching_token(buffer, window);
    if (created >= 0) {
        if (created > 0) {
            while (create_cursor_backward_matching_token(buffer, window) == 1) {
            }
        }
        if (create_cursor_forward_matching_token(buffer, window) == 1) {
            created = 1;
            while (create_cursor_forward_matching_token(buffer, window) == 1) {
            }
        }
    }

    show_created_messages(source.client, created);
}

}
}
