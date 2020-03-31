#include "commands.hpp"

#include <cz/defer.hpp>
#include <cz/option.hpp>
#include <cz/util.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "movement.hpp"
#include "transaction.hpp"
#include "visible_region_commands.hpp"

namespace mag {
namespace basic {

void command_set_mark(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        cursors[c].mark = cursors[c].point;
    }
    window->show_marks = true;
}

void command_swap_mark_point(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        cz::swap(cursors[c].point, cursors[c].mark);
    }
}

void command_forward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    TRANSFORM_POINTS(forward_char);
}

void command_backward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    TRANSFORM_POINTS(backward_char);
}

void command_forward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    TRANSFORM_POINTS(forward_word);
}

void command_backward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    TRANSFORM_POINTS(backward_word);
}

void command_forward_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    TRANSFORM_POINTS(forward_line);
}

void command_backward_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    TRANSFORM_POINTS(backward_line);
}

void command_end_of_buffer(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;
    uint64_t len = buffer->contents.len;
    for (size_t c = 0; c < cursors.len; ++c) {
        cursors[c].point = len;
    }
}

void command_start_of_buffer(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        cursors[c].point = 0;
    }
}

void command_end_of_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    TRANSFORM_POINTS(end_of_line);
}

void command_start_of_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    TRANSFORM_POINTS(start_of_line);
}

void command_start_of_line_text(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    TRANSFORM_POINTS(start_of_line_text);
}

void command_delete_backward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (source.previous_command == command_delete_backward_char) {
        CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len());
        Commit commit = buffer->commits[buffer->commit_index - 1];
        size_t len = commit.edits[0].value.len();
        if (len < SSOStr::MAX_SHORT_LEN) {
            cz::Slice<Cursor> cursors = window->cursors;

            CZ_DEBUG_ASSERT(commit.edits.len == cursors.len);
            buffer->undo();
            window->update_cursors(buffer->changes);

            Transaction transaction;
            transaction.init(commit.edits.len, 0);
            CZ_DEFER(transaction.drop());

            uint64_t offset = 1;
            for (size_t e = 0; e < commit.edits.len; ++e) {
                if (cursors[e].point == 0) {
                    continue;
                }

                CZ_DEBUG_ASSERT(commit.edits[e].value.is_short());
                CZ_DEBUG_ASSERT(commit.edits[e].value.len() == len);

                Edit edit;
                memcpy(edit.value.short_._buffer + 1, commit.edits[e].value.short_._buffer, len);
                edit.value.short_._buffer[0] =
                    buffer->contents.get_once(cursors[e].point - len - 1);
                edit.value.short_.set_len(len + 1);
                edit.position = commit.edits[e].position - offset;
                offset++;
                edit.flags = Edit::REMOVE;
                transaction.push(edit);
            }

            transaction.commit(buffer);
            return;
        }
    }

    DELETE_BACKWARD(backward_char);
}

void command_delete_forward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (source.previous_command == command_delete_forward_char) {
        CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len());
        Commit commit = buffer->commits[buffer->commit_index - 1];
        size_t len = commit.edits[0].value.len();
        if (len < SSOStr::MAX_SHORT_LEN) {
            cz::Slice<Cursor> cursors = window->cursors;

            CZ_DEBUG_ASSERT(commit.edits.len == cursors.len);
            buffer->undo();
            window->update_cursors(buffer->changes);

            Transaction transaction;
            transaction.init(commit.edits.len, 0);
            CZ_DEFER(transaction.drop());
            for (size_t e = 0; e < commit.edits.len; ++e) {
                CZ_DEBUG_ASSERT(commit.edits[e].value.is_short());
                CZ_DEBUG_ASSERT(commit.edits[e].value.len() == len);

                Edit edit;
                memcpy(edit.value.short_._buffer, commit.edits[e].value.short_._buffer, len);
                edit.value.short_._buffer[len] = buffer->contents.get_once(cursors[e].point + len);
                edit.value.short_.set_len(len + 1);
                edit.position = commit.edits[e].position - e;
                edit.flags = Edit::REMOVE_AFTER_POSITION;
                transaction.push(edit);
            }
            transaction.commit(buffer);
            return;
        }
    }

    DELETE_FORWARD(forward_char);
}

void command_delete_backward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    DELETE_BACKWARD(backward_word);
}

void command_delete_forward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    DELETE_FORWARD(forward_word);
}

void command_transpose_characters(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;
    Transaction transaction;
    transaction.init(2 * cursors.len, 0);
    CZ_DEFER(transaction.drop());

    for (size_t c = 0; c < cursors.len; ++c) {
        uint64_t point = cursors[c].point;
        if (point == 0 || point == buffer->contents.len) {
            continue;
        }

        Edit delete_forward;
        delete_forward.value.init_char(buffer->contents.get_once(point));
        delete_forward.position = point;
        delete_forward.flags = Edit::REMOVE_AFTER_POSITION;
        transaction.push(delete_forward);

        Edit insert_before;
        insert_before.value = delete_forward.value;
        insert_before.position = point - 1;
        insert_before.flags = Edit::INSERT;
        transaction.push(insert_before);
    }

    transaction.commit(buffer);
}

void command_open_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    insert_char(buffer, window, '\n');
    window->update_cursors(buffer->changes);
    TRANSFORM_POINTS(backward_char);
}

void command_insert_newline(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    insert_char(buffer, window, '\n');
}

void command_duplicate_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    uint64_t sum_region_sizes = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start;
        Contents_Iterator end;
        if (window->show_marks) {
            start = buffer->contents.iterator_at(cursors[c].start());
            end = buffer->contents.iterator_at(cursors[c].end());
            if (end.position > start.position) {
                end.retreat();
            }
        } else {
            start = buffer->contents.iterator_at(cursors[c].point);
            end = start;
        }
        start_of_line(&start);
        end_of_line(&end);
        sum_region_sizes += end.position - start.position;
    }

    Transaction transaction;
    transaction.init(cursors.len, sum_region_sizes + cursors.len);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start;
        Contents_Iterator end;
        if (window->show_marks) {
            start = buffer->contents.iterator_at(cursors[c].start());
            end = buffer->contents.iterator_at(cursors[c].end());
            if (end.position > start.position) {
                end.retreat();
            }
        } else {
            start = buffer->contents.iterator_at(cursors[c].point);
            end = start;
        }
        start_of_line(&start);
        end_of_line(&end);

        size_t region_size = end.position - start.position + 1;
        char* value = (char*)transaction.value_allocator().alloc({region_size, 1}).buffer;
        buffer->contents.slice_into(start, end.position, value);
        value[region_size - 1] = '\n';

        Edit edit;
        edit.value.init_from_constant({value, region_size});
        edit.position = start.position + offset;
        offset += edit.value.len();
        edit.flags = Edit::INSERT;
        transaction.push(edit);
    }

    transaction.commit(buffer);
}

void command_delete_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    uint64_t sum_region_sizes = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
        Contents_Iterator end = start;
        start_of_line(&start);
        end_of_line(&end);
        forward_char(&end);
        sum_region_sizes += end.position - start.position;
    }

    Transaction transaction;
    transaction.init(cursors.len, sum_region_sizes);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
        Contents_Iterator end = start;
        start_of_line(&start);
        end_of_line(&end);
        forward_char(&end);

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(), start, end.position);
        edit.position = start.position - offset;
        offset += edit.value.len();
        edit.flags = Edit::REMOVE;
        transaction.push(edit);
    }

    transaction.commit(buffer);
}

void command_delete_end_of_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    uint64_t sum_region_sizes = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
        Contents_Iterator end = start;
        end_of_line(&end);
        sum_region_sizes += end.position - start.position;
    }

    Transaction transaction;
    transaction.init(cursors.len, sum_region_sizes);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
        Contents_Iterator end = start;
        end_of_line(&end);

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(), start, end.position);
        edit.position = start.position - offset;
        offset += edit.value.len();
        edit.flags = Edit::REMOVE;
        transaction.push(edit);
    }

    transaction.commit(buffer);
}

void command_undo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!buffer->undo()) {
        source.client->show_message("Nothing to undo");
        return;
    }

    if (window->cursors.len() == 1) {
        uint64_t position = buffer->changes.last().commit.edits[0].position;
        window->cursors[0].point = position;
        center_in_window(window, buffer->contents.iterator_at(position));
    }
}

void command_redo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!buffer->redo()) {
        source.client->show_message("Nothing to rdo");
        return;
    }

    if (window->cursors.len() == 1) {
        uint64_t position = buffer->changes.last().commit.edits[0].position;
        window->cursors[0].point = position;
        center_in_window(window, buffer->contents.iterator_at(position));
    }
}

void command_stop_action(Editor* editor, Command_Source source) {
    bool done = false;

    Window_Unified* window = source.client->selected_window();
    if (!done && window->show_marks) {
        window->show_marks = false;
        done = true;
    }

    if (!done && window->cursors.len() > 1) {
        kill_extra_cursors(window, source.client);
        done = true;
    }

    if (!done && window->id == source.client->mini_buffer_window()->id) {
        source.client->hide_mini_buffer(editor);
        done = true;
    }

    source.client->show_message("Quit");
}

void command_quit(Editor* editor, Command_Source source) {
    source.client->queue_quit = true;
}

static void create_cursor_forward_line(Buffer* buffer, Window_Unified* window) {
    CZ_DEBUG_ASSERT(window->cursors.len() >= 1);
    Contents_Iterator last_cursor_iterator =
        buffer->contents.iterator_at(window->cursors.last().point);
    Contents_Iterator new_cursor_iterator = last_cursor_iterator;
    forward_line(&new_cursor_iterator);
    if (new_cursor_iterator.position != last_cursor_iterator.position) {
        Cursor cursor = {};
        cursor.point = new_cursor_iterator.position;
        cursor.mark = cursor.point;
        cursor.local_copy_chain = window->cursors.last().local_copy_chain;

        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.push(cursor);
    }
}

void command_create_cursor_forward_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    create_cursor_forward_line(buffer, window);
}

static void create_cursor_backward_line(Buffer* buffer, Window_Unified* window) {
    CZ_DEBUG_ASSERT(window->cursors.len() >= 1);
    Contents_Iterator first_cursor_iterator =
        buffer->contents.iterator_at(window->cursors[0].point);
    Contents_Iterator new_cursor_iterator = first_cursor_iterator;
    backward_line(&new_cursor_iterator);
    if (new_cursor_iterator.position != first_cursor_iterator.position) {
        Cursor cursor = {};
        cursor.point = new_cursor_iterator.position;
        cursor.mark = cursor.point;
        cursor.local_copy_chain = window->cursors[0].local_copy_chain;

        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.insert(0, cursor);
    }
}

void command_create_cursor_backward_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    create_cursor_backward_line(buffer, window);
}

static cz::Option<uint64_t> search_forward(Contents_Iterator start_it, cz::Str query) {
    for (; start_it.position + query.len < start_it.contents->len; start_it.advance()) {
        Contents_Iterator it = start_it;
        size_t q;
        for (q = 0; q < query.len; ++q) {
            if (it.get() != query[q]) {
                break;
            }
            it.advance();
        }

        if (q == query.len) {
            return start_it.position;
        }
    }

    return {};
}

static cz::Option<uint64_t> search_forward_slice(Buffer* buffer,
                                                 Contents_Iterator start,
                                                 uint64_t end) {
    if (start.at_eob()) {
        return {};
    }

    // Optimize: don't allocate when the slice is inside one bucket
    SSOStr slice = buffer->contents.slice(cz::heap_allocator(), start, end);
    CZ_DEFER(slice.drop(cz::heap_allocator()));

    start.advance();
    return search_forward(start, slice.as_str());
}

#define SEARCH_SLICE_THEN(FUNC, THEN)                                                            \
    do {                                                                                         \
        uint64_t start = cursors[c].start();                                                     \
        uint64_t end = cursors[c].end();                                                         \
        cz::Option<uint64_t> new_start = FUNC(buffer, buffer->contents.iterator_at(start), end); \
        if (new_start.is_present) {                                                              \
            Cursor new_cursor = {};                                                              \
            new_cursor.point = new_start.value;                                                  \
            new_cursor.mark = new_start.value + end - start;                                     \
            new_cursor.local_copy_chain = cursors[c].local_copy_chain;                           \
            if (cursors[c].point > cursors[c].mark) {                                            \
                cz::swap(new_cursor.point, new_cursor.mark);                                     \
            }                                                                                    \
            THEN;                                                                                \
        }                                                                                        \
    } while (0)

#define SEARCH_QUERY_THEN(FUNC, THEN)                                                      \
    do {                                                                                   \
        uint64_t start = cursors[c].point;                                                 \
        cz::Option<uint64_t> new_start = FUNC(buffer->contents.iterator_at(start), query); \
        if (new_start.is_present) {                                                        \
            Cursor new_cursor = {};                                                        \
            new_cursor.point = new_start.value + query.len;                                \
            new_cursor.mark = new_start.value;                                             \
            new_cursor.local_copy_chain = cursors[c].local_copy_chain;                     \
            THEN;                                                                          \
        }                                                                                  \
    } while (0)

static void create_cursor_forward_search(Buffer* buffer, Window_Unified* window) {
    cz::Slice<Cursor> cursors = window->cursors;
    CZ_DEBUG_ASSERT(cursors.len >= 1);
    size_t c = cursors.len - 1;
    SEARCH_SLICE_THEN(search_forward_slice, {
        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.push(new_cursor);
    });
}

void command_create_cursor_forward_search(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    create_cursor_forward_search(buffer, window);
}

static cz::Option<uint64_t> search_backward(Contents_Iterator start_it, cz::Str query) {
    if (query.len > start_it.contents->len) {
        return {};
    }

    if (start_it.contents->len - query.len < start_it.position) {
        start_it.retreat(start_it.position - (start_it.contents->len - query.len));
    }

    while (!start_it.at_bob()) {
        start_it.retreat();
        Contents_Iterator it = start_it;
        size_t q;
        for (q = 0; q < query.len; ++q) {
            if (it.get() != query[q]) {
                break;
            }
            it.advance();
        }

        if (q == query.len) {
            return start_it.position;
        }
    }

    return {};
}

static cz::Option<uint64_t> search_backward_slice(Buffer* buffer,
                                                  Contents_Iterator start,
                                                  uint64_t end) {
    if (start.at_bob()) {
        return {};
    }

    // Optimize: don't allocate when the slice is inside one bucket
    SSOStr slice = buffer->contents.slice(cz::heap_allocator(), start, end);
    CZ_DEFER(slice.drop(cz::heap_allocator()));

    start.retreat();
    return search_backward(start, slice.as_str());
}

static void create_cursor_backward_search(Buffer* buffer, Window_Unified* window) {
    cz::Slice<Cursor> cursors = window->cursors;
    CZ_DEBUG_ASSERT(cursors.len >= 1);
    size_t c = 0;
    SEARCH_SLICE_THEN(search_backward_slice, {
        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.insert(0, new_cursor);
    });
}

void command_create_cursor_backward_search(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    create_cursor_backward_search(buffer, window);
}

void command_create_cursor_forward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (window->show_marks) {
        create_cursor_forward_search(buffer, window);
    } else {
        create_cursor_forward_line(buffer, window);
    }
}

void command_create_cursor_backward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (window->show_marks) {
        create_cursor_backward_search(buffer, window);
    } else {
        create_cursor_backward_line(buffer, window);
    }
}

static void command_search_forward_callback(Editor* editor,
                                            Client* client,
                                            cz::Str query,
                                            void* data) {
    WITH_SELECTED_BUFFER(client);
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        SEARCH_QUERY_THEN(search_forward, {
            cursors[c] = new_cursor;
            window->show_marks = true;
        });
    }
}

void command_search_forward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (window->show_marks) {
        cz::Slice<Cursor> cursors = window->cursors;
        for (size_t c = 0; c < cursors.len; ++c) {
            SEARCH_SLICE_THEN(search_forward_slice, cursors[c] = new_cursor);
        }
    } else {
        source.client->show_dialog(editor, "Search forward: ", no_completion_engine,
                                   command_search_forward_callback, nullptr);
    }
}

static void command_search_backward_callback(Editor* editor,
                                             Client* client,
                                             cz::Str query,
                                             void* data) {
    WITH_SELECTED_BUFFER(client);
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        SEARCH_QUERY_THEN(search_backward, {
            cursors[c] = new_cursor;
            window->show_marks = true;
        });
    }
}

void command_search_backward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (window->show_marks) {
        cz::Slice<Cursor> cursors = window->cursors;
        for (size_t c = 0; c < cursors.len; ++c) {
            SEARCH_SLICE_THEN(search_backward_slice, cursors[c] = new_cursor);
        }
    } else {
        source.client->show_dialog(editor, "Search backward: ", no_completion_engine,
                                   command_search_backward_callback, nullptr);
    }
}

static void parse_number(cz::Str str, uint64_t* number) {
    for (size_t i = 0; i < str.len; ++i) {
        if (!isdigit(str[i])) {
            break;
        }
        *number *= 10;
        *number += str[i] - '0';
    }
}

static void command_goto_line_callback(Editor* editor, Client* client, cz::Str str, void* data) {
    uint64_t lines = 0;
    parse_number(str, &lines);

    WITH_SELECTED_BUFFER(client);
    Contents_Iterator iterator = buffer->contents.iterator_at(0);
    while (!iterator.at_eob() && lines > 1) {
        if (iterator.get() == '\n') {
            --lines;
        }
        iterator.advance();
    }

    window->cursors[0].point = iterator.position;
}

static void command_goto_position_callback(Editor* editor,
                                           Client* client,
                                           cz::Str str,
                                           void* data) {
    uint64_t position = 0;
    parse_number(str, &position);

    Window_Unified* window = client->selected_window();
    window->cursors[0].point = position;
}

void command_goto_line(Editor* editor, Command_Source source) {
    source.client->show_dialog(editor, "Goto line: ", no_completion_engine,
                               command_goto_line_callback, nullptr);
}

void command_goto_position(Editor* editor, Command_Source source) {
    source.client->show_dialog(editor, "Goto position: ", no_completion_engine,
                               command_goto_position_callback, nullptr);
}

void command_path_up_directory(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    Contents_Iterator start = buffer->contents.iterator_at(buffer->contents.len);
    if (start.at_bob()) {
        return;
    }
    start.retreat();
    while (1) {
        if (start.at_bob()) {
            return;
        }
        start.retreat();
        if (start.get() == '/') {
            start.advance();
            break;
        }
    }

    Transaction transaction;
    transaction.init(1, buffer->contents.len - start.position);

    Edit edit;
    edit.value = buffer->contents.slice(transaction.value_allocator(), start, buffer->contents.len);
    edit.position = start.position;
    edit.flags = Edit::REMOVE;
    transaction.push(edit);

    transaction.commit(buffer);
}

void command_mark_buffer(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    kill_extra_cursors(window, source.client);
    window->show_marks = true;
    window->cursors[0].mark = 0;
    WITH_WINDOW_BUFFER(window);
    window->cursors[0].point = buffer->contents.len;
}

}
}
