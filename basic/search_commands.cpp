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

}
}