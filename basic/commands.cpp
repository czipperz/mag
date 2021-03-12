#include "commands.hpp"

#include <ctype.h>
#include <cz/defer.hpp>
#include <cz/option.hpp>
#include <cz/util.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "insert.hpp"
#include "movement.hpp"
#include "transaction.hpp"
#include "visible_region.hpp"

namespace mag {
namespace basic {

void command_toggle_read_only(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    buffer->read_only = !buffer->read_only;
}

void command_toggle_pinned(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    window->pinned = !window->pinned;
}

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
    TRANSFORM_POINTS([&](Contents_Iterator* it) { forward_line(buffer->mode, it); });
}

void command_backward_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    TRANSFORM_POINTS([&](Contents_Iterator* it) { backward_line(buffer->mode, it); });
}

void command_end_of_buffer(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    push_jump(window, source.client, handle->id, buffer);
    window->cursors[0].point = buffer->contents.len;
}

void command_start_of_buffer(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    push_jump(window, source.client, handle->id, buffer);
    window->cursors[0].point = 0;
}

void command_push_jump(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    push_jump(window, source.client, handle->id, buffer);
}

void command_unpop_jump(Editor* editor, Command_Source source) {
    Jump* jump = source.client->jump_chain.unpop();
    if (jump) {
        goto_jump(editor, source.client, jump);
    }
}

void command_pop_jump(Editor* editor, Command_Source source) {
    if (source.client->jump_chain.index == source.client->jump_chain.jumps.len()) {
        WITH_SELECTED_BUFFER(source.client);
        push_jump(window, source.client, handle->id, buffer);
        source.client->jump_chain.pop();
    }

    Jump* jump = source.client->jump_chain.pop();
    if (jump) {
        goto_jump(editor, source.client, jump);
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

    if (buffer->check_last_committer(command_delete_backward_char, window->cursors)) {
        CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len());
        Commit commit = buffer->commits[buffer->commit_index - 1];
        CZ_DEBUG_ASSERT(commit.edits.len > 0);
        size_t len = commit.edits[commit.edits.len - 1].value.len();
        if (len < SSOStr::MAX_SHORT_LEN) {
            cz::Slice<Cursor> cursors = window->cursors;

            CZ_DEBUG_ASSERT(commit.edits.len == cursors.len);
            buffer->undo();
            window->update_cursors(buffer);

            Transaction transaction;
            transaction.init(commit.edits.len, 0);
            CZ_DEFER(transaction.drop());

            uint64_t offset = 1;
            for (size_t e = 0; e < commit.edits.len; ++e) {
                if (cursors[e].point == 0) {
                    continue;
                }

                CZ_DEBUG_ASSERT(commit.edits[e].value.is_short());
                CZ_DEBUG_ASSERT(commit.edits[e].value.len() <= len);

                if (cursors[e].point <= len) {
                    transaction.push(commit.edits[e]);
                    continue;
                }

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

            transaction.commit(buffer, command_delete_backward_char);
            return;
        }
    }

    DELETE_BACKWARD(backward_char, command_delete_backward_char);
}

void command_delete_forward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (buffer->check_last_committer(command_delete_forward_char, window->cursors)) {
        CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len());
        Commit commit = buffer->commits[buffer->commit_index - 1];
        CZ_DEBUG_ASSERT(commit.edits.len > 0);
        size_t len = commit.edits[0].value.len();
        if (len < SSOStr::MAX_SHORT_LEN) {
            cz::Slice<Cursor> cursors = window->cursors;

            CZ_DEBUG_ASSERT(commit.edits.len == cursors.len);
            buffer->undo();
            window->update_cursors(buffer);

            Transaction transaction;
            transaction.init(commit.edits.len, 0);
            CZ_DEFER(transaction.drop());
            for (size_t e = 0; e < commit.edits.len; ++e) {
                CZ_DEBUG_ASSERT(commit.edits[e].value.is_short());
                CZ_DEBUG_ASSERT(commit.edits[e].value.len() <= len);

                if (buffer->contents.len <= cursors[e].point + len) {
                    Edit edit = commit.edits[e];
                    edit.position -= e;
                    transaction.push(edit);
                    continue;
                }

                Edit edit;
                memcpy(edit.value.short_._buffer, commit.edits[e].value.short_._buffer, len);
                edit.value.short_._buffer[len] = buffer->contents.get_once(cursors[e].point + len);
                edit.value.short_.set_len(len + 1);
                edit.position = commit.edits[e].position - e;
                edit.flags = Edit::REMOVE_AFTER_POSITION;
                transaction.push(edit);
            }
            transaction.commit(buffer, command_delete_forward_char);
            return;
        }
    }

    DELETE_FORWARD(forward_char, command_delete_forward_char);
}

void command_delete_backward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    DELETE_BACKWARD(backward_word, nullptr);
}

void command_delete_forward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    DELETE_FORWARD(forward_word, nullptr);
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
        delete_forward.value = SSOStr::from_char(buffer->contents.get_once(point));
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
    window->update_cursors(buffer);
    TRANSFORM_POINTS(backward_char);
}

void command_insert_newline_no_indent(Editor* editor, Command_Source source) {
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
        char* value = (char*)transaction.value_allocator().alloc({region_size, 1});
        buffer->contents.slice_into(start, end.position, value);
        value[region_size - 1] = '\n';

        Edit edit;
        edit.value = SSOStr::from_constant({value, region_size});
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
        Contents_Iterator iterator = buffer->contents.iterator_at(position);
        if (!is_visible(window, iterator)) {
            window->cursors[0].point = position;
            center_in_window(window, iterator);
        }
    }
}

void command_redo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!buffer->redo()) {
        source.client->show_message("Nothing to redo");
        return;
    }

    if (window->cursors.len() == 1) {
        uint64_t position = buffer->changes.last().commit.edits[0].position;
        Contents_Iterator iterator = buffer->contents.iterator_at(position);
        if (!is_visible(window, iterator)) {
            window->cursors[0].point = position;
            center_in_window(window, iterator);
        }
    }
}

void command_stop_action(Editor* editor, Command_Source source) {
    bool done = false;

    Window_Unified* window = source.client->selected_window();
    if (!done && window->completing) {
        window->abort_completion();
        done = true;
    }

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

static void show_no_create_cursor_message(Client* client) {
    client->show_message("No more cursors to create");
}

static void show_no_region_message(Client* client) {
    client->show_message("Must select a non-empty region first");
}

static void show_created_messages(Client* client, int created) {
    if (created == -1) {
        show_no_region_message(client);
    }
    if (created == 0) {
        show_no_create_cursor_message(client);
    }
}

static bool create_cursor_forward_line(Editor* editor, Buffer* buffer, Window_Unified* window) {
    CZ_DEBUG_ASSERT(window->cursors.len() >= 1);
    Contents_Iterator last_cursor_iterator =
        buffer->contents.iterator_at(window->cursors.last().point);
    Contents_Iterator new_cursor_iterator = last_cursor_iterator;
    forward_line(buffer->mode, &new_cursor_iterator);
    if (new_cursor_iterator.position != last_cursor_iterator.position) {
        Cursor cursor = {};
        cursor.point = new_cursor_iterator.position;
        cursor.mark = cursor.point;
        cursor.local_copy_chain = window->cursors.last().local_copy_chain;

        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.push(cursor);
        return true;
    } else {
        return false;
    }
}

void command_create_cursor_forward_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!create_cursor_forward_line(editor, buffer, window)) {
        show_no_create_cursor_message(source.client);
    }
}

static bool create_cursor_backward_line(Editor* editor, Buffer* buffer, Window_Unified* window) {
    CZ_DEBUG_ASSERT(window->cursors.len() >= 1);
    Contents_Iterator first_cursor_iterator =
        buffer->contents.iterator_at(window->cursors[0].point);
    Contents_Iterator new_cursor_iterator = first_cursor_iterator;
    backward_line(buffer->mode, &new_cursor_iterator);
    if (new_cursor_iterator.position != first_cursor_iterator.position) {
        Cursor cursor = {};
        cursor.point = new_cursor_iterator.position;
        cursor.mark = cursor.point;
        cursor.local_copy_chain = window->cursors[0].local_copy_chain;

        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.insert(0, cursor);
        return true;
    } else {
        return false;
    }
}

void command_create_cursor_backward_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!create_cursor_backward_line(editor, buffer, window)) {
        show_no_create_cursor_message(source.client);
    }
}

static cz::Option<uint64_t> search_forward(Contents_Iterator start_it, cz::Str query) {
    for (; start_it.position + query.len <= start_it.contents->len; start_it.advance()) {
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
    CZ_DEBUG_ASSERT(end >= start.position);
    if (end + (end - start.position) > start.contents->len) {
        return {};
    }

    // Optimize: don't allocate when the slice is inside one bucket
    SSOStr slice = buffer->contents.slice(cz::heap_allocator(), start, end);
    CZ_DEFER(slice.drop(cz::heap_allocator()));

    start.advance_to(end);
    return search_forward(start, slice.as_str());
}

#define SEARCH_SLICE_THEN(FUNC, CREATED, THEN)                                                   \
    do {                                                                                         \
        uint64_t start = cursors[c].start();                                                     \
        uint64_t end = cursors[c].end();                                                         \
        cz::Option<uint64_t> new_start = FUNC(buffer, buffer->contents.iterator_at(start), end); \
        (CREATED) = new_start.is_present;                                                        \
        if (CREATED) {                                                                           \
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

static int create_cursor_forward_search(Buffer* buffer, Window_Unified* window) {
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
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_forward_search(buffer, window);
    show_created_messages(source.client, created);
}

static cz::Option<uint64_t> search_backward(Contents_Iterator start_it, cz::Str query) {
    if (query.len > start_it.contents->len) {
        return {};
    }

    if (start_it.contents->len - query.len < start_it.position) {
        start_it.retreat_to(start_it.contents->len - query.len);
    }

    while (1) {
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

        if (start_it.at_bob()) {
            break;
        }
        start_it.retreat();
    }

    return {};
}

static cz::Option<uint64_t> search_backward_slice(Buffer* buffer,
                                                  Contents_Iterator start,
                                                  uint64_t end) {
    CZ_DEBUG_ASSERT(end >= start.position);
    if (start.position < end - start.position) {
        return {};
    }

    // Optimize: don't allocate when the slice is inside one bucket
    SSOStr slice = buffer->contents.slice(cz::heap_allocator(), start, end);
    CZ_DEFER(slice.drop(cz::heap_allocator()));

    start.retreat(end - start.position);
    return search_backward(start, slice.as_str());
}

static int create_cursor_backward_search(Buffer* buffer, Window_Unified* window) {
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
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_search(buffer, window);
    show_created_messages(source.client, created);
}

void command_create_cursor_forward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created;
    if (window->show_marks) {
        created = create_cursor_forward_search(buffer, window);
    } else {
        created = create_cursor_forward_line(editor, buffer, window);
    }
    show_created_messages(source.client, created);
}

void command_create_cursor_backward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created;
    if (window->show_marks) {
        created = create_cursor_backward_search(buffer, window);
    } else {
        created = create_cursor_backward_line(editor, buffer, window);
    }
    show_created_messages(source.client, created);
}

void command_create_cursors_all_search(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_search(buffer, window);
    if (created >= 0) {
        if (created > 0) {
            while (create_cursor_backward_search(buffer, window) == 1) {
            }
        }
        if (create_cursor_forward_search(buffer, window) == 1) {
            created = 1;
            while (create_cursor_forward_search(buffer, window) == 1) {
            }
        }
    }

    show_created_messages(source.client, created);
}

void command_create_cursors_to_end_search(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_forward_search(buffer, window);
    if (created == 1) {
        while (create_cursor_forward_search(buffer, window) == 1) {
        }
    }
    show_created_messages(source.client, created);
}

void command_create_cursors_to_start_search(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_search(buffer, window);
    if (created == 1) {
        while (create_cursor_backward_search(buffer, window) == 1) {
        }
    }
    show_created_messages(source.client, created);
}

static void set_cursor_position_to_edit_redo(Cursor* cursor, const Edit* edit) {
    cursor->mark = edit->position;
    cursor->point = edit->position;
    if (edit->flags & Edit::INSERT_MASK) {
        cursor->point += edit->value.len();
    }
}

static void set_cursor_position_to_edit_undo(Cursor* cursor, const Edit* edit, int64_t* offset) {
    cursor->mark = edit->position + *offset;
    cursor->point = edit->position + *offset;
    if (edit->flags & Edit::INSERT_MASK) {
        // Undo logical insert = physical remove.
        *offset -= edit->value.len();
    } else {
        // Undo logical remove = physical insert.
        cursor->point += edit->value.len();
        *offset += edit->value.len();
    }
}

static void create_cursors_last_change(Window_Unified* window, Buffer* buffer, Client* client) {
    kill_extra_cursors(window, client);

    Change* change = &buffer->changes.last();
    cz::Slice<const Edit> edits = change->commit.edits;
    window->cursors.reserve(cz::heap_allocator(), edits.len - 1);

    if (change->is_redo) {
        set_cursor_position_to_edit_redo(&window->cursors[0], &edits[0]);

        Copy_Chain* local_copy_chain = window->cursors[0].local_copy_chain;
        for (size_t i = 1; i < edits.len; ++i) {
            Cursor cursor = {};
            set_cursor_position_to_edit_redo(&cursor, &edits[i]);
            cursor.local_copy_chain = local_copy_chain;
            window->cursors.push(cursor);
        }
    } else {
        int64_t offset = 0;
        set_cursor_position_to_edit_undo(&window->cursors[0], &edits[0], &offset);

        Copy_Chain* local_copy_chain = window->cursors[0].local_copy_chain;
        for (size_t i = 1; i < edits.len; ++i) {
            Cursor cursor = {};
            set_cursor_position_to_edit_undo(&cursor, &edits[i], &offset);
            cursor.local_copy_chain = local_copy_chain;
            window->cursors.push(cursor);
        }
    }
}

void command_create_cursors_undo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!buffer->undo()) {
        source.client->show_message("Nothing to undo");
        return;
    }

    window->update_cursors(buffer);
    create_cursors_last_change(window, buffer, source.client);
}

void command_create_cursors_redo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!buffer->redo()) {
        source.client->show_message("Nothing to redo");
        return;
    }

    window->update_cursors(buffer);
    create_cursors_last_change(window, buffer, source.client);
}

void command_create_cursors_last_change(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (buffer->changes.len() == 0) {
        return;
    }

    create_cursors_last_change(window, buffer, source.client);
}

void command_create_cursors_lines_in_region(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (window->cursors.len() > 1) {
        return;
    }

    Copy_Chain* local_copy_chain = window->cursors[0].local_copy_chain;

    uint64_t start = window->cursors[0].start();
    uint64_t end = window->cursors[0].end();

    Contents_Iterator iterator = buffer->contents.iterator_at(start);
    while (true) {
        forward_line(buffer->mode, &iterator);
        if (iterator.position >= end) {
            break;
        }

        Cursor cursor = {};
        cursor.point = cursor.mark = iterator.position;
        cursor.local_copy_chain = local_copy_chain;
        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.push(cursor);
    }

    Cursor new_cursor = {};
    new_cursor.point = new_cursor.mark = start;
    new_cursor.local_copy_chain = local_copy_chain;
    window->cursors[0] = new_cursor;

    window->show_marks = false;
}

void command_cursors_align(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;
    if (cursors.len == 1) {
        return;
    }

    Contents_Iterator iterator = buffer->contents.iterator_at(cursors[0].point);
    uint64_t min_column = 0;
    uint64_t max_column = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        iterator.advance_to(cursors[i].point);

        uint64_t col = get_visual_column(buffer->mode, iterator);

        if (i == 0 || col > max_column) {
            max_column = col;
        }
        if (i == 0 || col < min_column) {
            min_column = col;
        }
    }

    if (max_column == min_column) {
        return;
    }

    Transaction transaction;
    transaction.init(cursors.len - 1, max_column - min_column);
    CZ_DEFER(transaction.drop());

    char* buf = (char*)transaction.value_allocator().alloc({max_column - min_column, 1});
    CZ_DEBUG_ASSERT(buf);

    // Note: we fill with spaces as this is supposed to be used not to correctly indent lines but
    // rather to align things in those lines.  We don't want to start inserting tabs because then
    // they might look different on a different users screen.
    memset(buf, ' ', max_column - min_column);

    iterator.retreat_to(cursors[0].point);
    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        iterator.advance_to(cursors[i].point);

        uint64_t col = get_visual_column(buffer->mode, iterator);
        if (col == max_column) {
            continue;
        }

        Edit edit;
        edit.value = SSOStr::from_constant(cz::Str{buf, max_column - col});
        edit.position = cursors[i].point + offset;
        offset += max_column - col;
        edit.flags = Edit::INSERT;
        transaction.push(edit);
    }

    transaction.commit(buffer, command_cursors_align);
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
    Window_Unified* window = source.client->selected_window();
    if (window->show_marks) {
        cz::Slice<Cursor> cursors = window->cursors;
        WITH_WINDOW_BUFFER(window);
        for (size_t c = 0; c < cursors.len; ++c) {
            bool created;
            SEARCH_SLICE_THEN(search_forward_slice, created, cursors[c] = new_cursor);
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
    Window_Unified* window = source.client->selected_window();
    if (window->show_marks) {
        cz::Slice<Cursor> cursors = window->cursors;
        WITH_WINDOW_BUFFER(window);
        for (size_t c = 0; c < cursors.len; ++c) {
            bool created;
            SEARCH_SLICE_THEN(search_backward_slice, created, cursors[c] = new_cursor);
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
    push_jump(window, client, handle->id, buffer);

    Contents_Iterator iterator = start_of_line_position(buffer->contents, lines);
    window->cursors[0].point = iterator.position;
}

static void command_goto_position_callback(Editor* editor,
                                           Client* client,
                                           cz::Str str,
                                           void* data) {
    uint64_t position = 0;
    parse_number(str, &position);

    WITH_SELECTED_BUFFER(client);
    push_jump(window, client, handle->id, buffer);
    window->cursors[0].point = cz::min(position, buffer->contents.len);
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
            break;
        }
        start.retreat();
        if (start.get() == '/') {
            start.advance();
            break;
        }
    }

    Transaction transaction;
    transaction.init(1, buffer->contents.len - start.position);
    CZ_DEFER(transaction.drop());

    Edit edit;
    edit.value = buffer->contents.slice(transaction.value_allocator(), start, buffer->contents.len);
    edit.position = start.position;
    edit.flags = Edit::REMOVE;
    transaction.push(edit);

    transaction.commit(buffer);
}

void command_mark_buffer(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    push_jump(window, source.client, handle->id, buffer);
    window->show_marks = true;
    window->cursors[0].mark = 0;
    window->cursors[0].point = buffer->contents.len;
}

void command_submit_mini_buffer(Editor* editor, Command_Source source) {
    cz::Str mini_buffer_contents;
    {
        Window_Unified* window = source.client->mini_buffer_window();
        WITH_WINDOW_BUFFER(window);
        mini_buffer_contents = clear_buffer(buffer);
    }

    source.client->restore_selected_buffer();
    source.client->_message.response_callback(editor, source.client, mini_buffer_contents,
                                              source.client->_message.response_callback_data);
    source.client->dealloc_message();
}

}
}
