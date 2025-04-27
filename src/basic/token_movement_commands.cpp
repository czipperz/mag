#include "token_movement_commands.hpp"

#include <algorithm>
#include <cz/sort.hpp>
#include "core/command.hpp"
#include "core/command_macros.hpp"
#include "core/editor.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"
#include "cursor_commands.hpp"

namespace mag {
namespace basic {

////////////////////////////////////////////////////////////////////////////////
// Backward up token pair
////////////////////////////////////////////////////////////////////////////////

static void tokenize_recording_pairs(Tokenizer_Check_Point check_point,
                                     Contents_Iterator token_iterator,
                                     uint64_t end_position,
                                     Buffer* buffer,
                                     cz::Vector<Token>* tokens,
                                     bool non_pairs) {
    Token token;
    token.end = check_point.position;
    uint64_t state = check_point.state;
    while (1) {
        bool has_token = buffer->mode.next_token(&token_iterator, &token, &state);
        if (has_token) {
            if (token.end > end_position) {
                break;
            }
            if (token.type == Token_Type::OPEN_PAIR || token.type == Token_Type::CLOSE_PAIR ||
                (non_pairs && (token.type == Token_Type::PREPROCESSOR_IF ||
                               token.type == Token_Type::PREPROCESSOR_ENDIF))) {
                tokens->reserve(cz::heap_allocator(), 1);
                tokens->push(token);
            }
        } else {
            break;
        }
    }
}

bool backward_up_token_pair(Buffer* buffer, Contents_Iterator* cursor, bool non_pairs) {
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
        tokenize_recording_pairs(check_point, token_iterator, end_position, buffer, &tokens,
                                 non_pairs);

        for (size_t i = tokens.len; i-- > 0;) {
            if (tokens[i].type == Token_Type::OPEN_PAIR ||
                (non_pairs && tokens[i].type == Token_Type::PREPROCESSOR_IF)) {
                if (depth == 0) {
                    cursor->retreat_to(tokens[i].start);
                    return true;
                }

                --depth;
            } else {
                ++depth;
            }
        }

        if (check_point_index == 0) {
            return false;
        }

        --check_point_index;
        end_position = check_point.position;
        check_point = buffer->token_cache.check_points[check_point_index];
        tokens.len = 0;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Get token before/after position
////////////////////////////////////////////////////////////////////////////////

bool get_token_after_position(Buffer* buffer,
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

bool get_token_before_position(Buffer* buffer,
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
            uint64_t previous_state = *state;
            bool has_token = buffer->mode.next_token(token_iterator, token, state);
            if (!has_token || token->start >= end_position) {
                *token = previous_token;
                *state = previous_state;
                token_iterator->retreat_to(previous_token.end);
                return true;
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Forward up token pair
////////////////////////////////////////////////////////////////////////////////

bool forward_up_token_pair(Buffer* buffer, Contents_Iterator* cursor, bool non_pair) {
    Contents_Iterator token_iterator = *cursor;

    Token token;
    uint64_t state;
    if (!get_token_after_position(buffer, &token_iterator, &state, &token)) {
        return false;
    }

    size_t depth = 0;
    while (1) {
        if (token.type == Token_Type::CLOSE_PAIR ||
            (non_pair && token.type == Token_Type::PREPROCESSOR_ENDIF)) {
            if (depth == 0) {
                cursor->advance_to(token.end);
                return true;
            }

            --depth;
        } else if (token.type == Token_Type::OPEN_PAIR ||
                   (non_pair && token.type == Token_Type::PREPROCESSOR_IF)) {
            ++depth;
        }

        bool has_token = buffer->mode.next_token(&token_iterator, &token, &state);
        if (!has_token) {
            return false;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Forward/backward up token pair command wrappers
////////////////////////////////////////////////////////////////////////////////

static void sort_cursors(cz::Slice<Cursor> cursors) {
    cz::sort(cursors,
             [](const Cursor* left, const Cursor* right) { return left->point < right->point; });
}

REGISTER_COMMAND(command_backward_up_token_pair);
void command_backward_up_token_pair(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();

    for (size_t cursor_index = window->cursors.len; cursor_index-- > 0;) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        backward_up_token_pair(buffer, &it, /*non_pair=*/true);
        window->cursors[cursor_index].point = it.position;
    }

    sort_cursors(window->cursors);
}

REGISTER_COMMAND(command_forward_up_token_pair);
void command_forward_up_token_pair(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();

    for (size_t cursor_index = 0; cursor_index < window->cursors.len; ++cursor_index) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        forward_up_token_pair(buffer, &it, /*non_pair=*/true);
        window->cursors[cursor_index].point = it.position;
    }

    sort_cursors(window->cursors);
}

////////////////////////////////////////////////////////////////////////////////
// Forward/backward up token pair respecting indent
////////////////////////////////////////////////////////////////////////////////

bool backward_up_token_pair_or_indent(Buffer* buffer, Contents_Iterator* cursor, bool non_pairs) {
    if (backward_up_token_pair(buffer, cursor, non_pairs))
        return true;

    Contents_Iterator it = *cursor;

    // When at an empty line, assume it is part of the prefix for the next statement.
    while (!it.at_eob() && at_empty_line(it)) {
        it.advance();
    }

    start_of_line(&it);

    Contents_Iterator sol = it;
    start_of_line_text(&sol);
    uint64_t start = get_visual_column(buffer->mode, sol);
    if (start == 0)
        return false;

    while (!it.at_bob()) {
        it.retreat();

        // Skip empty lines because they aren't top level declarations.
        while (!it.at_bob() && at_empty_line(it)) {
            it.retreat();
        }

        start_of_line(&it);

        Contents_Iterator sol = it;
        start_of_line_text(&sol);
        uint64_t col = get_visual_column(buffer->mode, sol);
        if (col < start) {
            if (sol.position < cursor->position) {
                *cursor = sol;
                return true;
            } else {
                // If we go forward then just abort.  This happens when
                // at an empty line between two top level declarations.
                return false;
            }
        }
    }
    return false;
}

bool forward_up_token_pair_or_indent(Buffer* buffer, Contents_Iterator* cursor, bool non_pairs) {
    if (forward_up_token_pair(buffer, cursor, non_pairs))
        return true;

    Contents_Iterator it = *cursor;

    // When at an empty line, assume it is part of the prefix for the next statement.
    while (!it.at_eob() && at_empty_line(it)) {
        it.advance();
    }

    start_of_line(&it);

    Contents_Iterator sol = it;
    start_of_line_text(&sol);
    uint64_t start = get_visual_column(buffer->mode, sol);
    if (start == 0)
        return false;

    while (1) {
        end_of_line(&it);
        forward_char(&it);

        // Skip empty lines because they aren't top level declarations.
        while (!it.at_eob() && at_empty_line(it)) {
            it.advance();
        }

        if (it.at_eob())
            break;

        Contents_Iterator sol = it;
        start_of_line_text(&sol);
        uint64_t col = get_visual_column(buffer->mode, sol);
        if (col < start) {
            // Go backwards through empty lines.  This puts the cursor at the
            // end of this declaration instead of at the start of the next one.
            it.retreat();
            if (at_start_of_line(it)) {
                while (!it.at_bob() && at_start_of_line(it)) {
                    it.retreat();
                }
                if (!it.at_bob())
                    it.advance();
                sol = it;
            }

            if (sol.position > cursor->position) {
                *cursor = sol;
                return true;
            } else {
                // If we go forward then just abort.  This happens when
                // at an empty line between two top level declarations.
                return false;
            }
        }
    }
    return false;
}

REGISTER_COMMAND(command_backward_up_token_pair_or_indent);
void command_backward_up_token_pair_or_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();

    for (size_t cursor_index = window->cursors.len; cursor_index-- > 0;) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        backward_up_token_pair_or_indent(buffer, &it, /*non_pair=*/true);
        window->cursors[cursor_index].point = it.position;
    }

    sort_cursors(window->cursors);
}

REGISTER_COMMAND(command_forward_up_token_pair_or_indent);
void command_forward_up_token_pair_or_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();

    for (size_t cursor_index = 0; cursor_index < window->cursors.len; ++cursor_index) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        forward_up_token_pair_or_indent(buffer, &it, /*non_pair=*/true);
        window->cursors[cursor_index].point = it.position;
    }

    sort_cursors(window->cursors);
}

////////////////////////////////////////////////////////////////////////////////
// Token pair
////////////////////////////////////////////////////////////////////////////////

bool forward_token_pair(Buffer* buffer, Contents_Iterator* iterator, bool non_pair) {
    Token token;
    uint64_t state;
    Contents_Iterator backup = *iterator;
    if (!get_token_after_position(buffer, iterator, &state, &token)) {
        *iterator = backup;
        return false;
    }

    CZ_DEBUG_ASSERT(token.end == iterator->position);

    if (token.type == Token_Type::OPEN_PAIR ||
        (non_pair && token.type == Token_Type::PREPROCESSOR_IF)) {
        forward_up_token_pair(buffer, iterator, /*non_pair=*/true);
    }

    return true;
}

REGISTER_COMMAND(command_forward_token_pair);
void command_forward_token_pair(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();

    for (size_t cursor_index = 0; cursor_index < window->cursors.len; ++cursor_index) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        forward_token_pair(buffer, &it, /*non_pair=*/true);
        window->cursors[cursor_index].point = it.position;
    }

    sort_cursors(window->cursors);
}

////////////////////////////////////////////////////////////////////////////////

bool backward_token_pair(Buffer* buffer, Contents_Iterator* iterator, bool non_pair) {
    Token token;
    uint64_t state;
    Contents_Iterator backup = *iterator;
    if (!get_token_before_position(buffer, iterator, &state, &token)) {
        *iterator = backup;
        return false;
    }

    iterator->retreat_to(token.start);

    if (token.type == Token_Type::CLOSE_PAIR ||
        (non_pair && token.type == Token_Type::PREPROCESSOR_ENDIF)) {
        backward_up_token_pair(buffer, iterator, /*non_pair=*/true);
    }

    return true;
}

REGISTER_COMMAND(command_backward_token_pair);
void command_backward_token_pair(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();

    for (size_t cursor_index = 0; cursor_index < window->cursors.len; ++cursor_index) {
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[cursor_index].point);
        backward_token_pair(buffer, &it, /*non_pair=*/true);
        window->cursors[cursor_index].point = it.position;
    }

    sort_cursors(window->cursors);
}

////////////////////////////////////////////////////////////////////////////////
// Matching token
////////////////////////////////////////////////////////////////////////////////

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
    if (buffer->token_cache.find_check_point(token_to_match.start, &check_point_index)) {
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
        if ((token.type == Token_Type::DEFAULT) == (token_to_match.type == Token_Type::DEFAULT) &&
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

REGISTER_COMMAND(command_backward_matching_token);
void command_backward_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    for (size_t cursor_index = 0; cursor_index < window->cursors.len; ++cursor_index) {
        Cursor* cursor = &window->cursors[cursor_index];
        Contents_Iterator it = buffer->contents.iterator_at(cursor->point);
        backward_matching_token(buffer, &it);

        int64_t mark_offset = cursor->mark - cursor->point;
        cursor->point = it.position;
        cursor->mark = cursor->point;

        if (mark_offset < 0 && cursor->point < (uint64_t)-mark_offset)
            cursor->mark = 0;
        else if (mark_offset > 0 && cursor->point + mark_offset > buffer->contents.len)
            cursor->mark = buffer->contents.len;
        else
            cursor->mark += mark_offset;
    }

    sort_cursors(window->cursors);
}

////////////////////////////////////////////////////////////////////////////////

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
            if ((token.type == Token_Type::DEFAULT) ==
                    (token_to_match.type == Token_Type::DEFAULT) &&
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

REGISTER_COMMAND(command_forward_matching_token);
void command_forward_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    for (size_t cursor_index = 0; cursor_index < window->cursors.len; ++cursor_index) {
        Cursor* cursor = &window->cursors[cursor_index];
        Contents_Iterator it = buffer->contents.iterator_at(cursor->point);
        forward_matching_token(buffer, &it);

        int64_t mark_offset = cursor->mark - cursor->point;
        cursor->point = it.position;
        cursor->mark = cursor->point;

        if (mark_offset < 0 && cursor->point < (uint64_t)-mark_offset)
            cursor->mark = 0;
        else if (mark_offset > 0 && cursor->point + mark_offset > buffer->contents.len)
            cursor->mark = buffer->contents.len;
        else
            cursor->mark += mark_offset;
    }

    sort_cursors(window->cursors);
}

////////////////////////////////////////////////////////////////////////////////
// Create cursor matching token
////////////////////////////////////////////////////////////////////////////////

static Cursor create_cursor_with_offsets(Cursor cursor, Contents_Iterator it) {
    Cursor new_cursor = {};
    new_cursor.local_copy_chain = cursor.local_copy_chain;
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
    Cursor cursor = window->cursors[window->cursors.len - 1];
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

static void show_created_messages_token(Client* client, int created) {
    if (created == -1) {
        client->show_message("Cursor is not positioned at a token");
    }
    if (created == 0) {
        client->show_message("No more cursors to create");
    }
}

REGISTER_COMMAND(command_create_cursor_forward_matching_token);
void command_create_cursor_forward_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_forward_matching_token(buffer, window);
    show_created_messages_token(source.client, created);

    if (created == 1 && window->selected_cursor + 1 == window->cursors.len - 1) {
        ++window->selected_cursor;
    }
}

REGISTER_COMMAND(command_create_cursor_backward_matching_token);
void command_create_cursor_backward_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_matching_token(buffer, window);
    show_created_messages_token(source.client, created);

    if (created == 1 && window->selected_cursor > 0) {
        ++window->selected_cursor;
    }
}

REGISTER_COMMAND(command_create_cursors_to_end_matching_token);
void command_create_cursors_to_end_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_forward_matching_token(buffer, window);
    if (created == 1) {
        if (window->selected_cursor + 1 == window->cursors.len - 1) {
            ++window->selected_cursor;
        }
        while (create_cursor_forward_matching_token(buffer, window) == 1) {
            if (window->selected_cursor + 1 == window->cursors.len - 1) {
                ++window->selected_cursor;
            }
        }
    }
    show_created_messages_token(source.client, created);
}

REGISTER_COMMAND(command_create_cursors_to_start_matching_token);
void command_create_cursors_to_start_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_matching_token(buffer, window);
    if (created == 1) {
        if (window->selected_cursor != 0) {
            ++window->selected_cursor;
        }
        while (create_cursor_backward_matching_token(buffer, window) == 1) {
            if (window->selected_cursor != 0) {
                ++window->selected_cursor;
            }
        }
    }
    show_created_messages_token(source.client, created);
}

REGISTER_COMMAND(command_create_all_cursors_matching_token);
void command_create_all_cursors_matching_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_matching_token(buffer, window);
    if (created >= 0) {
        if (created > 0) {
            ++window->selected_cursor;
            while (create_cursor_backward_matching_token(buffer, window) == 1) {
                ++window->selected_cursor;
            }
        }
        if (create_cursor_forward_matching_token(buffer, window) == 1) {
            created = 1;
            while (create_cursor_forward_matching_token(buffer, window) == 1) {
            }
        }
    }

    show_created_messages_token(source.client, created);
}

REGISTER_COMMAND(command_create_all_cursors_matching_token_or_search);
void command_create_all_cursors_matching_token_or_search(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    if (window->show_marks) {
        return command_create_cursors_all_search(editor, source);
    } else {
        return command_create_all_cursors_matching_token(editor, source);
    }
}

////////////////////////////////////////////////////////////////////////////////
// Delete token
////////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_delete_token);
void command_delete_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction = {};
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;
    Contents_Iterator it = buffer->contents.start();
    for (size_t c = 0; c < cursors.len; ++c) {
        it.go_to(cursors[c].point);

        Token token;
        if (!get_token_at_position(buffer, &it, &token)) {
            it.go_to(cursors[c].point);
            uint64_t state;
            if (!get_token_after_position(buffer, &it, &state, &token)) {
                continue;
            }
        }

        it.retreat_to(std::min(token.start, cursors[c].point));

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(), it, token.end);
        edit.flags = Edit::REMOVE;
        edit.position = it.position - offset;
        offset += token.end - it.position;
        transaction.push(edit);
    }

    transaction.commit(source.client);
}

REGISTER_COMMAND(command_delete_forward_token);
void command_delete_forward_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction = {};
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;
    Contents_Iterator it = buffer->contents.start();
    for (size_t c = 0; c < cursors.len; ++c) {
        it.go_to(cursors[c].point);

        Token token;
        uint64_t state;
        if (!get_token_after_position(buffer, &it, &state, &token)) {
            break;
        }

        it.retreat_to(cursors[c].point);

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(), it, token.end);
        edit.flags = Edit::REMOVE;
        edit.position = it.position - offset;
        offset += token.end - it.position;
        transaction.push(edit);
    }

    transaction.commit(source.client);
}

REGISTER_COMMAND(command_delete_backward_token);
void command_delete_backward_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction = {};
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;
    Contents_Iterator it = buffer->contents.start();
    for (size_t c = 0; c < cursors.len; ++c) {
        it.go_to(cursors[c].point);

        Token token;
        uint64_t state;
        if (!get_token_before_position(buffer, &it, &state, &token)) {
            break;
        }

        it.retreat_to(token.start);

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(), it, cursors[c].point);
        edit.flags = Edit::REMOVE;
        edit.position = it.position - offset;
        offset += cursors[c].point - it.position;
        transaction.push(edit);
    }

    transaction.commit(source.client);
}

////////////////////////////////////////////////////////////////////////////////
// Duplicate token
////////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_duplicate_token);
void command_duplicate_token(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction = {};
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;
    Contents_Iterator it = buffer->contents.start();
    for (size_t c = 0; c < cursors.len; ++c) {
        if (window->show_marks) {
            it.go_to(cursors[c].start());

            Edit edit;
            edit.value =
                buffer->contents.slice(transaction.value_allocator(), it, cursors[c].end());
            edit.flags = Edit::INSERT;
            if (cursors[c].point >= cursors[c].mark) {
                edit.flags = Edit::INSERT_AFTER_POSITION;
            }
            edit.position = cursors[c].point + offset;
            offset += edit.value.len();
            transaction.push(edit);
        } else {
            it.go_to(cursors[c].point);

            Token token;
            if (!get_token_at_position(buffer, &it, &token)) {
                continue;
            }

            cursors[c].point = token.end;
            it.retreat_to(token.start);

            Edit edit;
            edit.value = buffer->contents.slice(transaction.value_allocator(), it, token.end);
            edit.flags = Edit::INSERT_AFTER_POSITION;
            edit.position = cursors[c].point + offset;
            offset += token.end - token.start;
            transaction.push(edit);
        }
    }
    window->show_marks = false;

    transaction.commit(source.client);
}

////////////////////////////////////////////////////////////////////////////////
// Transpose token
////////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_transpose_tokens);
void command_transpose_tokens(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction = {};
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Slice<Cursor> cursors = window->cursors;
    Contents_Iterator it = buffer->contents.start();
    for (size_t c = 0; c < cursors.len; ++c) {
        it.go_to(cursors[c].point);

        uint64_t state;
        Token before_token;
        if (!get_token_before_position(buffer, &it, &state, &before_token)) {
            continue;
        }

        Token after_token;
        if (!buffer->mode.next_token(&it, &after_token, &state)) {
            continue;
        }

        it.go_to(before_token.start);
        SSOStr value1 = buffer->contents.slice(transaction.value_allocator(), it, before_token.end);
        it.advance_to(after_token.start);
        SSOStr value2 = buffer->contents.slice(transaction.value_allocator(), it, after_token.end);

        Edit remove1;
        remove1.value = value1;
        remove1.position = before_token.start;
        remove1.flags = Edit::REMOVE;
        transaction.push(remove1);
        Edit insert2;
        insert2.value = value2;
        insert2.position = before_token.start;
        insert2.flags = Edit::INSERT;
        transaction.push(insert2);

        Edit remove2;
        remove2.value = value2;
        remove2.position = after_token.start + value2.len() - value1.len();
        remove2.flags = Edit::REMOVE;
        transaction.push(remove2);
        Edit insert1;
        insert1.value = value1;
        insert1.position = after_token.start + value2.len() - value1.len();
        insert1.flags = Edit::INSERT;
        transaction.push(insert1);
    }

    transaction.commit(source.client);
}

}
}
