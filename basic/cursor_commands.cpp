#include "cursor_commands.hpp"

#include "command_macros.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "search_commands.hpp"

namespace mag {
namespace basic {

static void show_no_create_cursor_message(Client* client) {
    client->show_message("No more cursors to create");
}

static void show_no_region_message(Client* client) {
    client->show_message("Must select a non-empty region first");
}

void show_created_messages(Client* client, int created) {
    if (created == -1) {
        show_no_region_message(client);
    }
    if (created == 0) {
        show_no_create_cursor_message(client);
    }
}

static bool create_cursor_forward_line(Editor* editor,
                                       const Buffer* buffer,
                                       Window_Unified* window) {
    CZ_DEBUG_ASSERT(window->cursors.len >= 1);
    Contents_Iterator new_cursor_iterator =
        buffer->contents.iterator_at(window->cursors.last().point);
    if (!forward_line(buffer->mode, &new_cursor_iterator))
        return false;

    Cursor cursor = {};
    cursor.point = new_cursor_iterator.position;
    cursor.mark = cursor.point;
    cursor.local_copy_chain = window->cursors.last().local_copy_chain;

    window->cursors.reserve(cz::heap_allocator(), 1);
    window->cursors.push(cursor);
    return true;
}

REGISTER_COMMAND(command_create_cursor_forward_line);
void command_create_cursor_forward_line(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    if (!create_cursor_forward_line(editor, buffer, window)) {
        show_no_create_cursor_message(source.client);
    }
}

static bool create_cursor_backward_line(Editor* editor,
                                        const Buffer* buffer,
                                        Window_Unified* window) {
    CZ_DEBUG_ASSERT(window->cursors.len >= 1);
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

REGISTER_COMMAND(command_create_cursor_backward_line);
void command_create_cursor_backward_line(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    if (!create_cursor_backward_line(editor, buffer, window)) {
        show_no_create_cursor_message(source.client);
    }
}

REGISTER_COMMAND(command_create_cursor_forward);
void command_create_cursor_forward(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    int created;
    if (window->show_marks) {
        created = create_cursor_forward_search(buffer, window);
    } else {
        created = create_cursor_forward_line(editor, buffer, window);
    }
    show_created_messages(source.client, created);

    if (created == 1 && window->selected_cursor + 1 == window->cursors.len - 1) {
        ++window->selected_cursor;
    }
}

REGISTER_COMMAND(command_create_cursor_backward);
void command_create_cursor_backward(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    int created;
    if (window->show_marks) {
        created = create_cursor_backward_search(buffer, window);
    } else {
        created = create_cursor_backward_line(editor, buffer, window);
    }
    show_created_messages(source.client, created);

    if (created == 1 && window->selected_cursor > 0) {
        ++window->selected_cursor;
    }
}

REGISTER_COMMAND(command_create_cursors_all_search);
void command_create_cursors_all_search(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_search(buffer, window);
    if (created >= 0) {
        if (created > 0) {
            ++window->selected_cursor;
            while (create_cursor_backward_search(buffer, window) == 1) {
                ++window->selected_cursor;
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

REGISTER_COMMAND(command_create_cursors_to_end_search);
void command_create_cursors_to_end_search(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    bool is_last = (window->selected_cursor == window->cursors.len - 1);
    int created = create_cursor_forward_search(buffer, window);
    if (created == 1) {
        while (create_cursor_forward_search(buffer, window) == 1) {
        }
    }

    show_created_messages(source.client, created);

    if (created == 1 && is_last) {
        window->selected_cursor = window->cursors.len - 1;
    }
}

REGISTER_COMMAND(command_create_cursors_to_start_search);
void command_create_cursors_to_start_search(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    bool is_first = (window->selected_cursor == 0);
    int created = create_cursor_backward_search(buffer, window);
    if (created == 1) {
        ++window->selected_cursor;
        while (create_cursor_backward_search(buffer, window) == 1) {
            ++window->selected_cursor;
        }
    }

    show_created_messages(source.client, created);

    if (created == 1 && is_first) {
        window->selected_cursor = 0;
    }
}

static void command_filter_cursors_looking_at_callback(Editor* editor,
                                                       Client* client,
                                                       cz::Str query,
                                                       void* _data) {
    WITH_CONST_SELECTED_BUFFER(client);

    Contents_Iterator it = buffer->contents.start();

    // Calculate number remaining.
    size_t num_remaining = 0;
    for (size_t i = 0; i < window->cursors.len; ++i) {
        it.advance_to(window->cursors[i].point);
        num_remaining += looking_at(it, query);
    }

    // If everything would die then handle it gracefully.
    if (num_remaining == 0) {
        kill_extra_cursors(window, client);
        return;
    }

    // Delete non-matching cursors.
    ++window->selected_cursor;
    for (size_t i = window->cursors.len; i-- > 0;) {
        it.retreat_to(window->cursors[i].point);
        if (!looking_at(it, query)) {
            window->cursors.remove(i);
            if (window->selected_cursor > i) {
                --window->selected_cursor;
            }
        }
    }
    if (window->selected_cursor > 0) {
        --window->selected_cursor;
    }
}

static void command_filter_cursors_not_looking_at_callback(Editor* editor,
                                                           Client* client,
                                                           cz::Str query,
                                                           void* _data) {
    WITH_CONST_SELECTED_BUFFER(client);

    Contents_Iterator it = buffer->contents.start();

    // Calculate number remaining.
    size_t num_remaining = 0;
    for (size_t i = 0; i < window->cursors.len; ++i) {
        it.advance_to(window->cursors[i].point);
        num_remaining += !looking_at(it, query);
    }

    // If everything would die then handle it gracefully.
    if (num_remaining == 0) {
        kill_extra_cursors(window, client);
        return;
    }

    // Delete non-matching cursors.
    ++window->selected_cursor;
    for (size_t i = window->cursors.len; i-- > 0;) {
        it.retreat_to(window->cursors[i].point);
        if (looking_at(it, query)) {
            window->cursors.remove(i);
            if (window->selected_cursor > i) {
                --window->selected_cursor;
            }
        }
    }
    if (window->selected_cursor > 0) {
        --window->selected_cursor;
    }
}

REGISTER_COMMAND(command_filter_cursors_looking_at);
void command_filter_cursors_looking_at(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Filter looking at: ";
    dialog.response_callback = command_filter_cursors_looking_at_callback;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_filter_cursors_not_looking_at);
void command_filter_cursors_not_looking_at(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Filter not looking at: ";
    dialog.response_callback = command_filter_cursors_not_looking_at_callback;
    source.client->show_dialog(dialog);
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

static void create_cursors_redo(Window_Unified* window,
                                const Buffer* buffer,
                                Client* client,
                                cz::Slice<const Edit> edits) {
    kill_extra_cursors(window, client);
    window->cursors.reserve(cz::heap_allocator(), edits.len - 1);

    set_cursor_position_to_edit_redo(&window->cursors[0], &edits[0]);

    Copy_Chain* local_copy_chain = window->cursors[0].local_copy_chain;
    for (size_t i = 1; i < edits.len; ++i) {
        Cursor cursor = {};
        set_cursor_position_to_edit_redo(&cursor, &edits[i]);
        cursor.local_copy_chain = local_copy_chain;
        window->cursors.push(cursor);
    }
}

static void create_cursors_undo(Window_Unified* window,
                                const Buffer* buffer,
                                Client* client,
                                cz::Slice<const Edit> edits) {
    kill_extra_cursors(window, client);
    window->cursors.reserve(cz::heap_allocator(), edits.len - 1);

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

static void create_cursors_last_change(Window_Unified* window,
                                       const Buffer* buffer,
                                       Client* client) {
    const Change* change = &buffer->changes.last();
    cz::Slice<const Edit> edits = change->commit.edits;

    if (change->is_redo) {
        create_cursors_redo(window, buffer, client, edits);
    } else {
        create_cursors_undo(window, buffer, client, edits);
    }
}

REGISTER_COMMAND(command_create_cursors_undo_nono);
void command_create_cursors_undo_nono(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (buffer->commit_index == 0) {
        source.client->show_message("Nothing to undo");
        return;
    }

    Commit commit = buffer->commits[buffer->commit_index - 1];
    window->update_cursors(buffer);
    create_cursors_undo(window, buffer, source.client, commit.edits);
}

REGISTER_COMMAND(command_create_cursors_undo);
void command_create_cursors_undo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!buffer->undo()) {
        source.client->show_message("Nothing to undo");
        return;
    }

    window->update_cursors(buffer);
    create_cursors_last_change(window, buffer, source.client);
}

REGISTER_COMMAND(command_create_cursors_redo_nono);
void command_create_cursors_redo_nono(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (buffer->commit_index + 1 >= buffer->commits.len) {
        source.client->show_message("Nothing to redo");
        return;
    }

    Commit commit = buffer->commits[buffer->commit_index - 1];
    window->update_cursors(buffer);
    create_cursors_redo(window, buffer, source.client, commit.edits);
}

REGISTER_COMMAND(command_create_cursors_redo);
void command_create_cursors_redo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!buffer->redo()) {
        source.client->show_message("Nothing to redo");
        return;
    }

    window->update_cursors(buffer);
    create_cursors_last_change(window, buffer, source.client);
}

REGISTER_COMMAND(command_create_cursors_last_change);
void command_create_cursors_last_change(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    if (buffer->changes.len == 0) {
        return;
    }

    create_cursors_last_change(window, buffer, source.client);
}

REGISTER_COMMAND(command_create_cursors_lines_in_region);
void command_create_cursors_lines_in_region(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    if (window->cursors.len > 1 || !window->show_marks) {
        uint64_t start = window->cursors[0].start();
        uint64_t end = window->cursors.last().end();

        kill_extra_cursors(window, source.client);

        Contents_Iterator it = buffer->contents.iterator_at(end);
        forward_line(buffer->mode, &it);

        window->cursors[0].mark = start;
        window->cursors[0].point = it.position;
        window->show_marks = true;
        return;
    }

    Copy_Chain* local_copy_chain = window->cursors[0].local_copy_chain;

    uint64_t start = window->cursors[0].start();
    uint64_t end = window->cursors[0].end();

    Contents_Iterator iterator = buffer->contents.iterator_at(start);
    while (true) {
        if (!forward_line(buffer->mode, &iterator)) {
            break;
        }
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

REGISTER_COMMAND(command_cursors_align);
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
    transaction.init(buffer);
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

    transaction.commit(source.client, command_cursors_align);
}

REGISTER_COMMAND(command_cursors_align_leftpad0);
void command_cursors_align_leftpad0(Editor* editor, Command_Source source) {
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

        while (!iterator.at_eob()) {
            if (!cz::is_digit(iterator.get()))
                break;
            iterator.advance();
        }

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
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    char* buf = (char*)transaction.value_allocator().alloc({max_column - min_column, 1});
    CZ_DEBUG_ASSERT(buf);

    // Note: we fill with spaces as this is supposed to be used not to correctly indent lines but
    // rather to align things in those lines.  We don't want to start inserting tabs because then
    // they might look different on a different users screen.
    memset(buf, '0', max_column - min_column);

    iterator.retreat_to(cursors[0].point);
    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        iterator.advance_to(cursors[i].point);

        while (!iterator.at_eob()) {
            if (!cz::is_digit(iterator.get()))
                break;
            iterator.advance();
        }

        uint64_t col = get_visual_column(buffer->mode, iterator);
        if (col == max_column) {
            continue;
        }

        Edit edit;
        edit.value = SSOStr::from_constant(cz::Str{buf, max_column - col});
        edit.position = cursors[i].point + offset;
        offset += max_column - col;
        edit.flags = Edit::INSERT_AFTER_POSITION;
        transaction.push(edit);
    }

    transaction.commit(source.client, command_cursors_align);
}

REGISTER_COMMAND(command_remove_cursors_at_empty_lines);
void command_remove_cursors_at_empty_lines(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    size_t count_cursors = 0;
    Contents_Iterator iterator = buffer->contents.start();
    for (size_t c = 0; c < window->cursors.len; ++c) {
        iterator.advance_to(window->cursors[c].point);
        if (at_empty_line(iterator)) {
            ++count_cursors;
        }
    }

    if (count_cursors == window->cursors.len) {
        kill_extra_cursors(window, source.client);
        return;
    }

    iterator.go_to(window->cursors[0].point);
    for (size_t c = 0; c < window->cursors.len;) {
        iterator.advance_to(window->cursors[c].point);
        if (at_empty_line(iterator)) {
            window->cursors.remove(c);
            continue;
        }
        ++c;
    }

    if (window->cursors.len == 1) {
        kill_extra_cursors(window, source.client);
    }
}

REGISTER_COMMAND(command_remove_selected_cursor);
void command_remove_selected_cursor(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();

    // No cursor to kill.
    if (window->cursors.len == 1) {
        return;
    }

    // When going down to 1 cursor we need to call `kill_extra_cursors` to cleanup various settings.
    if (window->cursors.len == 2) {
        window->selected_cursor = 1 - window->selected_cursor;
        kill_extra_cursors(window, source.client);
        return;
    }

    // Remove the selected cursor and make sure it is still in bounds.
    window->cursors.remove(window->selected_cursor);
    if (window->selected_cursor == window->cursors.len) {
        --window->selected_cursor;
    }
}

}
}
