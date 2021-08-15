#include "search_commands.hpp"

#include "command_macros.hpp"
#include "match.hpp"
#include "movement.hpp"

namespace mag {
namespace basic {

void submit_mini_buffer(Editor* editor, Client* client);
void show_created_messages(Editor* editor, Client* client, int created);

static bool search_forward_slice(const Buffer* buffer, Contents_Iterator* start, uint64_t end) {
    CZ_DEBUG_ASSERT(end >= start->position);
    if (end + (end - start->position) > start->contents->len) {
        return {};
    }

    // Optimize: don't allocate when the slice is inside one bucket
    SSOStr slice = buffer->contents.slice(cz::heap_allocator(), *start, end);
    CZ_DEFER(slice.drop(cz::heap_allocator()));

    start->advance_to(end);
    return search_forward_cased(start, slice.as_str(), buffer->mode.search_continue_case_handling);
}

#define SEARCH_SLICE_THEN(FUNC, CREATED, THEN)                             \
    do {                                                                   \
        uint64_t start = cursors[c].start();                               \
        uint64_t end = cursors[c].end();                                   \
        Contents_Iterator new_start = buffer->contents.iterator_at(start); \
        (CREATED) = FUNC(buffer, &new_start, end);                         \
        if (CREATED) {                                                     \
            Cursor new_cursor = {};                                        \
            new_cursor.point = new_start.position;                         \
            new_cursor.mark = new_start.position + end - start;            \
            new_cursor.local_copy_chain = cursors[c].local_copy_chain;     \
            if (cursors[c].point > cursors[c].mark) {                      \
                cz::swap(new_cursor.point, new_cursor.mark);               \
            }                                                              \
            THEN;                                                          \
        }                                                                  \
    } while (0)

#define SEARCH_QUERY_THEN(FUNC, THEN)                                                 \
    do {                                                                              \
        uint64_t start = cursors[c].point;                                            \
        Contents_Iterator new_start = buffer->contents.iterator_at(start);            \
        for (i = 0; i < n; ++i) {                                                     \
            if (i > 0) {                                                              \
                if (FUNC == search_forward_cased) {                                   \
                    forward_char(&new_start);                                         \
                } else {                                                              \
                    backward_char(&new_start);                                        \
                }                                                                     \
            }                                                                         \
            if (!FUNC(&new_start, query, buffer->mode.search_prompt_case_handling)) { \
                break;                                                                \
            }                                                                         \
        }                                                                             \
                                                                                      \
        if (i == n) {                                                                 \
            Cursor new_cursor = {};                                                   \
            new_cursor.point = new_start.position + query.len;                        \
            new_cursor.mark = new_start.position;                                     \
            new_cursor.local_copy_chain = cursors[c].local_copy_chain;                \
            THEN;                                                                     \
        }                                                                             \
    } while (0)

int create_cursor_forward_search(const Buffer* buffer, Window_Unified* window) {
    cz::Slice<Cursor> cursors = window->cursors;
    CZ_DEBUG_ASSERT(cursors.len >= 1);
    size_t c = cursors.len - 1;
    if (!window->show_marks || cursors[c].mark == cursors[c].point) {
        return -1;
    }
    bool created;
    SEARCH_SLICE_THEN(search_forward_slice, created, {
        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.push(new_cursor);
    });
    return created;
}

REGISTER_COMMAND(command_create_cursor_forward_search);
void command_create_cursor_forward_search(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    int created = create_cursor_forward_search(buffer, window);
    show_created_messages(editor, source.client, created);

    if (created == 1 && window->selected_cursor + 1 == window->cursors.len - 1) {
        ++window->selected_cursor;
    }
}

static bool search_backward_slice(const Buffer* buffer, Contents_Iterator* start, uint64_t end) {
    CZ_DEBUG_ASSERT(end >= start->position);
    if (start->position < end - start->position) {
        return {};
    }

    // Optimize: don't allocate when the slice is inside one bucket
    SSOStr slice = buffer->contents.slice(cz::heap_allocator(), *start, end);
    CZ_DEFER(slice.drop(cz::heap_allocator()));

    start->retreat(end - start->position);
    return search_backward_cased(start, slice.as_str(), buffer->mode.search_continue_case_handling);
}

int create_cursor_backward_search(const Buffer* buffer, Window_Unified* window) {
    cz::Slice<Cursor> cursors = window->cursors;
    CZ_DEBUG_ASSERT(cursors.len >= 1);
    size_t c = 0;
    if (!window->show_marks || cursors[c].mark == cursors[c].point) {
        return -1;
    }
    bool created;
    SEARCH_SLICE_THEN(search_backward_slice, created, {
        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.insert(0, new_cursor);
    });
    return created;
}

REGISTER_COMMAND(command_create_cursor_backward_search);
void command_create_cursor_backward_search(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_search(buffer, window);
    show_created_messages(editor, source.client, created);

    if (created == 1 && window->selected_cursor > 0) {
        ++window->selected_cursor;
    }
}

struct Interactive_Search_Data {
    int64_t direction;
    uint64_t cursor_point;
    uint64_t cursor_mark;
    uint64_t mini_buffer_change_index;
};

static void interactive_search_reset(Window_Unified* window, Interactive_Search_Data* data) {
    window->cursors[0].point = data->cursor_point;
    window->cursors[0].mark = data->cursor_mark;
    window->show_marks = false;
}

static void interactive_search_cancel(Editor* editor, Client* client, void* _data) {
    Interactive_Search_Data* data = (Interactive_Search_Data*)_data;
    Window_Unified* window = client->selected_normal_window;
    interactive_search_reset(window, data);
}

static void interactive_search_response_callback(Editor* editor,
                                                 Client* client,
                                                 cz::Str query,
                                                 void* _data) {
    ZoneScoped;

    Interactive_Search_Data* data = (Interactive_Search_Data*)_data;

    // If the mini buffer hasn't changed then we're already at the result.
    {
        WITH_CONST_WINDOW_BUFFER(client->_mini_buffer);
        if (data->mini_buffer_change_index == buffer->changes.len) {
            return;
        }

        data->mini_buffer_change_index = buffer->changes.len;
    }

    Window_Unified* window = client->selected_normal_window;
    interactive_search_reset(window, data);

    WITH_CONST_WINDOW_BUFFER(window);
    cz::Slice<Cursor> cursors = window->cursors;
    size_t c = 0;
    size_t i = 0;
retry:
    if (data->direction >= 1) {
        size_t n = data->direction;
        SEARCH_QUERY_THEN(search_forward_cased, {
            cursors[0] = new_cursor;
            window->show_marks = true;
        });

        if (i == 0) {
            i = 1;
        }

        if (i != n) {
            data->direction = i;
            // Retry for case where we go off the end.
            goto retry;
        }
    } else {
        size_t n = 1 - data->direction;
        SEARCH_QUERY_THEN(search_backward_cased, {
            cursors[0] = new_cursor;
            window->show_marks = true;
        });

        if (i == 0) {
            i = 1;
        }

        if (i != n) {
            data->direction = 1 - i;
            // Retry for case where we go off the end.
            goto retry;
        }
    }
}

static void command_search_forward_callback(Editor* editor,
                                            Client* client,
                                            cz::Str query,
                                            void* _data) {
    WITH_CONST_SELECTED_BUFFER(client);
    size_t n = 1;
    if (_data) {
        Interactive_Search_Data* data = (Interactive_Search_Data*)_data;
        interactive_search_reset(window, data);
        n = data->direction;
    }

    cz::Slice<Cursor> cursors = window->cursors;
    size_t i = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        SEARCH_QUERY_THEN(search_forward_cased, {
            // Only push jump if we hit a result, are the only cursor, and not at either sob
            // or eob.  If we don't hit a result the cursor doesn't move so push_jump just
            // adds a useless jump.  If we are at sob or eob then there is already a jump
            // and we don't want to create an intermediary jump they have top page through.
            if (window->cursors.len == 1 && window->cursors[0].point != 0 &&
                window->cursors[0].point != buffer->contents.len) {
                push_jump(window, client, buffer);
            }

            cursors[c] = new_cursor;
            window->show_marks = true;
        });
    }
}

static void command_search_backward_callback(Editor* editor,
                                             Client* client,
                                             cz::Str query,
                                             void* _data) {
    WITH_CONST_SELECTED_BUFFER(client);
    size_t n = 1;
    if (_data) {
        Interactive_Search_Data* data = (Interactive_Search_Data*)_data;
        interactive_search_reset(window, data);
        n = 1 - data->direction;
    }

    cz::Slice<Cursor> cursors = window->cursors;
    size_t i = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        SEARCH_QUERY_THEN(search_backward_cased, {
            // Only push jump if we hit a result, are the only cursor, and not at either sob
            // or eob.  If we don't hit a result the cursor doesn't move so push_jump just
            // adds a useless jump.  If we are at sob or eob then there is already a jump
            // and we don't want to create an intermediary jump they have top page through.
            if (window->cursors.len == 1 && window->cursors[0].point != 0 &&
                window->cursors[0].point != buffer->contents.len) {
                push_jump(window, client, buffer);
            }

            cursors[c] = new_cursor;
            window->show_marks = true;
        });
    }
}

bool in_interactive_search(Client* client) {
    return client->_select_mini_buffer &&
           client->_message.interactive_response_callback == interactive_search_response_callback;
}

REGISTER_COMMAND(command_search_forward);
void command_search_forward(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_normal_window;

    // Already prompting.
    if (in_interactive_search(source.client)) {
        auto data = (Interactive_Search_Data*)source.client->_message.response_callback_data;
        ++data->direction;
        data->mini_buffer_change_index = 0;
        if (data->direction == 1) {
            source.client->set_prompt_text("Search forward: ");
            source.client->_message.response_callback = command_search_forward_callback;
        }
        return;
    }

    // If not interactively prompting then deal with the mini buffer.
    if (source.client->_select_mini_buffer) {
        if (source.client->_message.response_callback == command_search_backward_callback ||
            source.client->_message.response_callback == command_search_forward_callback) {
            submit_mini_buffer(editor, source.client);
        } else {
            source.client->hide_mini_buffer(editor);
        }
    }

    if (window->show_marks) {
        // Search using the matching region.
        cz::Slice<Cursor> cursors = window->cursors;
        WITH_CONST_WINDOW_BUFFER(window);
        for (size_t c = 0; c < cursors.len; ++c) {
            bool created;
            SEARCH_SLICE_THEN(search_forward_slice, created, cursors[c] = new_cursor);
        }
    } else {
        // Search using a prompt.
        Dialog dialog = {};
        dialog.prompt = "Search forward: ";
        dialog.response_callback = command_search_forward_callback;
        {
            WITH_CONST_WINDOW_BUFFER(window);
            dialog.next_token = buffer->mode.next_token;
        }

        // Interactive search.
        if (window->cursors.len == 1) {
            Interactive_Search_Data* data = cz::heap_allocator().alloc<Interactive_Search_Data>();
            CZ_ASSERT(data);
            data->direction = 1;
            data->cursor_point = window->cursors[0].point;
            data->cursor_mark = window->cursors[0].mark;
            data->mini_buffer_change_index = 0;

            dialog.interactive_response_callback = interactive_search_response_callback;
            dialog.response_cancel = interactive_search_cancel;
            dialog.response_callback_data = data;
        }

        source.client->show_dialog(dialog);
    }
}

REGISTER_COMMAND(command_search_backward);
void command_search_backward(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_normal_window;

    // Already prompting.
    if (in_interactive_search(source.client)) {
        auto data = (Interactive_Search_Data*)source.client->_message.response_callback_data;
        --data->direction;
        data->mini_buffer_change_index = 0;
        if (data->direction == 0) {
            source.client->set_prompt_text("Search backward: ");
            source.client->_message.response_callback = command_search_backward_callback;
        }
        return;
    }

    // If not interactively prompting then deal with the mini buffer.
    if (source.client->_select_mini_buffer) {
        if (source.client->_message.response_callback == command_search_backward_callback ||
            source.client->_message.response_callback == command_search_forward_callback) {
            submit_mini_buffer(editor, source.client);
        } else {
            source.client->hide_mini_buffer(editor);
        }
    }

    if (window->show_marks) {
        // Search using the matching region.
        cz::Slice<Cursor> cursors = window->cursors;
        WITH_CONST_WINDOW_BUFFER(window);
        for (size_t c = 0; c < cursors.len; ++c) {
            bool created;
            SEARCH_SLICE_THEN(search_backward_slice, created, cursors[c] = new_cursor);
        }
    } else {
        // Search using a prompt.
        Dialog dialog = {};
        dialog.prompt = "Search backward: ";
        dialog.response_callback = command_search_backward_callback;
        {
            WITH_CONST_WINDOW_BUFFER(window);
            dialog.next_token = buffer->mode.next_token;
        }

        // Interactive search.
        if (window->cursors.len == 1) {
            Interactive_Search_Data* data = cz::heap_allocator().alloc<Interactive_Search_Data>();
            CZ_ASSERT(data);
            data->direction = 0;
            data->cursor_point = window->cursors[0].point;
            data->cursor_mark = window->cursors[0].mark;
            data->mini_buffer_change_index = 0;

            dialog.interactive_response_callback = interactive_search_response_callback;
            dialog.response_cancel = interactive_search_cancel;
            dialog.response_callback_data = data;
        }

        source.client->show_dialog(dialog);
    }
}

static void command_search_backward_expanding_callback(Editor* editor,
                                                       Client* client,
                                                       cz::Str query,
                                                       void* _data) {
    WITH_CONST_SELECTED_BUFFER(client);

    cz::Slice<Cursor> cursors = window->cursors;
    if (!window->show_marks) {
        for (size_t c = 0; c < cursors.len; ++c) {
            cursors[c].mark = cursors[c].point;
        }
    }

    size_t n = 1;
    size_t i = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        SEARCH_QUERY_THEN(search_backward_cased, cursors[c].point = new_cursor.mark);
    }
    window->show_marks = true;
}

static void command_search_forward_expanding_callback(Editor* editor,
                                                      Client* client,
                                                      cz::Str query,
                                                      void* _data) {
    WITH_CONST_SELECTED_BUFFER(client);

    cz::Slice<Cursor> cursors = window->cursors;
    if (!window->show_marks) {
        for (size_t c = 0; c < cursors.len; ++c) {
            cursors[c].mark = cursors[c].point;
        }
    }

    size_t n = 1;
    size_t i = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        SEARCH_QUERY_THEN(search_forward_cased, cursors[c].point = new_cursor.point);
    }
    window->show_marks = true;
}

REGISTER_COMMAND(command_search_backward_expanding);
void command_search_backward_expanding(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Search backward expanding: ";
    dialog.response_callback = command_search_backward_expanding_callback;
    {
        WITH_CONST_WINDOW_BUFFER(source.client->selected_normal_window);
        dialog.next_token = buffer->mode.next_token;
    }
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_search_forward_expanding);
void command_search_forward_expanding(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Search forward expanding: ";
    dialog.response_callback = command_search_forward_expanding_callback;
    {
        WITH_CONST_WINDOW_BUFFER(source.client->selected_normal_window);
        dialog.next_token = buffer->mode.next_token;
    }
    source.client->show_dialog(dialog);
}

///////////////////////////////////////////////////////////////////////////////
// search_{backward/forward}_identifier implementation
///////////////////////////////////////////////////////////////////////////////

/// Find identifier in bucket.
static bool look_in(cz::Slice<char> bucket,
                    cz::Str identifier,
                    Contents_Iterator* out,
                    bool forward) {
    ZoneScoped;
    const cz::Str str = {bucket.elems, bucket.len};
    const char first = identifier[0];
    Contents_Iterator test_start = *out;
    if (!forward)
        test_start.advance(str.len);
    size_t index = forward ? 0 : str.len;
    bool first_iteration = true;
    while (1) {
        // Find the start of the test identifier.
        const char* fst;
        if (forward)
            fst = str.slice_start(index).find(first);
        else
            fst = str.slice_end(index).rfind(first);
        if (!fst)
            break;

        size_t old_index = index;
        index = fst - str.buffer;
        if (forward) {
            ++index;
            if (first_iteration) {
                first_iteration = false;
            } else {
                test_start.advance();
            }
        }

        if (forward)
            test_start.advance(fst - (str.buffer + old_index));
        else
            test_start.retreat((str.buffer + old_index) - fst);

        // If character before is an identifier character then
        // `test_start` is not at the start of an identifier.
        if (!test_start.at_bob()) {
            Contents_Iterator temp = test_start;
            temp.retreat();
            char before = temp.get();
            if (cz::is_alnum(before) || before == '_')
                continue;
        }

        // Check character after region is not an identifier.
        if (test_start.position + identifier.len >= test_start.contents->len) {
            if (test_start.position + identifier.len > test_start.contents->len)
                continue;
        } else {
            Contents_Iterator test_end = test_start;
            test_end.advance(identifier.len);
            CZ_DEBUG_ASSERT(test_end.position < test_end.contents->len);
            char after = test_end.get();
            if (cz::is_alnum(after) || after == '_')
                continue;
        }

        // Check matches.
        if (!looking_at(test_start, identifier))
            continue;

        *out = test_start;
        return true;
    }
    return false;
}

static bool search_backward_identifier(Contents_Iterator* iterator, cz::Str query) {
    Contents_Iterator backward;
    backward = *iterator;
    backward.retreat(backward.index);

    // Search in the shared bucket.
    if (iterator->bucket < iterator->contents->buckets.len) {
        cz::Slice<char> bucket = iterator->contents->buckets[iterator->bucket];
        bool match_backward = look_in({bucket.elems, iterator->index}, query, &backward, false);
        if (match_backward) {
            *iterator = backward;
            return true;
        }
    }

    while (backward.bucket > 0) {
        cz::Slice<char> bucket = iterator->contents->buckets[backward.bucket - 1];
        backward.retreat(bucket.len);
        bool match_backward = look_in(bucket, query, &backward, false);
        if (match_backward) {
            *iterator = backward;
            return true;
        }
    }

    return false;
}

static bool search_forward_identifier(Contents_Iterator* iterator, cz::Str query) {
    Contents_Iterator forward;
    forward = *iterator;
    forward.retreat(forward.index);

    // Search in the shared bucket.
    if (iterator->bucket < iterator->contents->buckets.len) {
        cz::Slice<char> bucket = iterator->contents->buckets[iterator->bucket];
        Contents_Iterator temp = forward;
        temp.advance(iterator->index + 1);
        cz::Slice<char> after = {bucket.elems + iterator->index + 1,
                                 bucket.len - iterator->index - 1};
        bool match_forward = look_in(after, query, &temp, true);
        if (match_forward) {
            *iterator = temp;
            return true;
        }
    }

    while (forward.bucket + 1 < iterator->contents->buckets.len) {
        forward.advance(iterator->contents->buckets[forward.bucket].len);
        cz::Slice<char> bucket = iterator->contents->buckets[forward.bucket];
        bool match_forward = look_in(bucket, query, &forward, true);
        if (match_forward) {
            *iterator = forward;
            return true;
        }
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////
// search_{backward/forward}_identifier wrappers
///////////////////////////////////////////////////////////////////////////////

static bool search_backward_identifier_cased(Contents_Iterator* iterator,
                                             cz::Str query,
                                             Case_Handling case_handling) {
    return search_backward_identifier(iterator, query);
}

static bool search_forward_identifier_cased(Contents_Iterator* iterator,
                                            cz::Str query,
                                            Case_Handling case_handling) {
    return search_forward_identifier(iterator, query);
}

static void command_search_backward_identifier_callback(Editor* editor,
                                                        Client* client,
                                                        cz::Str query,
                                                        void* _data) {
    if (query.len == 0) {
        client->show_message("Error: empty query");
        return;
    }

    WITH_CONST_SELECTED_BUFFER(client);

    cz::Slice<Cursor> cursors = window->cursors;
    size_t n = 1;
    size_t i = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        SEARCH_QUERY_THEN(search_backward_identifier_cased, {
            cursors[c].point = new_cursor.mark;
        });
    }
    window->show_marks = false;
}

static void command_search_forward_identifier_callback(Editor* editor,
                                                       Client* client,
                                                       cz::Str query,
                                                       void* _data) {
    WITH_CONST_SELECTED_BUFFER(client);

    if (query.len == 0) {
        client->show_message("Error: empty query");
        return;
    }

    cz::Slice<Cursor> cursors = window->cursors;
    size_t n = 1;
    size_t i = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        SEARCH_QUERY_THEN(search_forward_identifier_cased, {
            cursors[c].point = new_cursor.mark;
        });
    }
    window->show_marks = false;
}

REGISTER_COMMAND(command_search_backward_identifier);
void command_search_backward_identifier(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Search backward identifier: ";
    dialog.response_callback = command_search_backward_identifier_callback;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_search_forward_identifier);
void command_search_forward_identifier(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Search forward identifier: ";
    dialog.response_callback = command_search_forward_identifier_callback;
    source.client->show_dialog(dialog);
}

}
}