#include "commands.hpp"

#include <cz/char_type.hpp>
#include <cz/compare.hpp>
#include <cz/defer.hpp>
#include <cz/format.hpp>
#include <cz/option.hpp>
#include <cz/parse.hpp>
#include <cz/path.hpp>
#include <cz/process.hpp>
#include <cz/sort.hpp>
#include <cz/util.hpp>
#include "command_macros.hpp"
#include "comment.hpp"
#include "file.hpp"
#include "insert.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "program_info.hpp"
#include "region_movement_commands.hpp"
#include "search_commands.hpp"
#include "transaction.hpp"
#include "visible_region.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

REGISTER_COMMAND(command_do_nothing);
void command_do_nothing(Editor* editor, Command_Source source) {}

REGISTER_COMMAND(command_invalid);
void command_invalid(Editor* editor, Command_Source source) {
    // Print a message that this key press failed.
    cz::String message = {};
    CZ_DEFER(message.drop(cz::heap_allocator()));

    cz::Str prefix;
    if (source.keys.len == 1) {
        prefix = "Unbound key:";
    } else {
        prefix = "Unbound key chain:";
    }
    message.reserve(cz::heap_allocator(),
                    prefix.len + (1 + stringify_key_max_size) * source.keys.len);
    message.append(prefix);

    CZ_DEBUG_ASSERT(source.keys.len > 0);
    for (size_t i = 0; i < source.keys.len; ++i) {
        message.push(' ');
        stringify_key(&message, source.keys[i]);
    }

    source.client->show_message(message);
}

REGISTER_COMMAND(command_show_marks);
void command_show_marks(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    window->show_marks = true;
}

REGISTER_COMMAND(command_set_mark);
void command_set_mark(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        cursors[c].mark = cursors[c].point;
    }
    window->show_marks = true;
}

REGISTER_COMMAND(command_swap_mark_point);
void command_swap_mark_point(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        cz::swap(cursors[c].point, cursors[c].mark);
    }

    cz::sort(cursors,
             [](const Cursor* left, const Cursor* right) { return left->point < right->point; });
}

REGISTER_COMMAND(command_push_jump);
void command_push_jump(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    push_jump(window, source.client, buffer);
}

REGISTER_COMMAND(command_unpop_jump);
void command_unpop_jump(Editor* editor, Command_Source source) {
    while (1) {
        Jump* jump = source.client->jump_chain.unpop();
        if (!jump) {
            source.client->show_message("No more jumps");
            break;
        }

        if (goto_jump(editor, source.client, jump)) {
            break;
        }

        // Invalid jump so kill it and retry.
        source.client->jump_chain.pop();
        source.client->jump_chain.kill_this_jump();
    }
}

REGISTER_COMMAND(command_pop_jump);
void command_pop_jump(Editor* editor, Command_Source source) {
    if (source.client->jump_chain.index == source.client->jump_chain.jumps.len) {
        WITH_CONST_SELECTED_BUFFER(source.client);
        push_jump(window, source.client, buffer);
        source.client->jump_chain.pop();
    }

    if (!pop_jump(editor, source.client)) {
        source.client->show_message("No more jumps");
    }
}

REGISTER_COMMAND(command_delete_backward_char);
void command_delete_backward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    // If temporarily showing marks then just delete the region instead.
    if (window->show_marks == 2) {
        delete_regions(source.client, buffer, window);
        window->show_marks = false;
        return;
    }

    // See if there are any cursors that are going to delete tabs.
    cz::Slice<Cursor> cursors = window->cursors;
    Contents_Iterator it = buffer->contents.start();
    size_t tabs = 0;
    for (size_t e = 0; e < cursors.len; ++e) {
        if (cursors[e].point == 0) {
            continue;
        }

        it.advance_to(cursors[e].point);

        it.retreat();
        if (it.get() == '\t') {
            ++tabs;
        }
    }

    // Treat tabs as if they were spaces when doing deletion.
    int64_t offset = 0;
    if (tabs > 0 && buffer->mode.tab_width > 1) {
        Transaction transaction;
        transaction.init(buffer);
        CZ_DEFER(transaction.drop());

        char* buf = (char*)transaction.value_allocator().alloc({buffer->mode.tab_width - 1, 1});
        memset(buf, ' ', buffer->mode.tab_width - 1);

        if (cursors.len > 1) {
            it.retreat_to(cursors[0].point);
        }
        for (size_t c = 0; c < cursors.len; ++c) {
            if (cursors[c].point == 0) {
                continue;
            }

            it.advance_to(cursors[c].point);

            it.retreat();

            // Remove the character.
            Edit remove;
            remove.position = it.position + offset;
            remove.value = SSOStr::from_char(it.get());
            remove.flags = Edit::REMOVE;
            transaction.push(remove);

            // And if it was a tab insert a bunch of spaces.
            if (it.get() == '\t') {
                uint64_t column = get_visual_column(buffer->mode, it);
                size_t len = buffer->mode.tab_width - 1 - column % buffer->mode.tab_width;

                Edit insert;
                insert.position = it.position + offset;
                insert.value = SSOStr::from_constant({buf, len});
                insert.flags = Edit::INSERT;
                transaction.push(insert);

                offset += len;
            }

            --offset;
        }

        // Don't merge edits around tab replacement.
        transaction.commit(source.client);
        return;
    }

    if (!editor->theme.insert_replace &&
        source.previous_command.function == command_delete_backward_char &&
        buffer->check_last_committer(command_delete_backward_char, window->cursors)) {
        CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len);
        Commit commit = buffer->commits[buffer->commit_index - 1];
        CZ_DEBUG_ASSERT(commit.edits.len > 0);
        size_t len = commit.edits[commit.edits.len - 1].value.len();
        if (len < SSOStr::MAX_SHORT_LEN) {
            cz::Slice<Cursor> cursors = window->cursors;

            CZ_DEBUG_ASSERT(commit.edits.len == cursors.len);
            buffer->undo();
            window->update_cursors(buffer);

            Transaction transaction;
            transaction.init(buffer);
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

            transaction.commit(source.client, command_delete_backward_char);
            return;
        }
    }

    DELETE_BACKWARD(backward_char, command_delete_backward_char);
}

REGISTER_COMMAND(command_delete_forward_char);
void command_delete_forward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    // If temporarily showing marks then just delete the region instead.
    if (window->show_marks == 2) {
        delete_regions(source.client, buffer, window);
        window->show_marks = false;
        return;
    }

    if (source.previous_command.function == command_delete_forward_char &&
        buffer->check_last_committer(command_delete_forward_char, window->cursors)) {
        CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len);
        Commit commit = buffer->commits[buffer->commit_index - 1];
        CZ_DEBUG_ASSERT(commit.edits.len > 0);
        size_t len = commit.edits[0].value.len();
        if (len < SSOStr::MAX_SHORT_LEN) {
            cz::Slice<Cursor> cursors = window->cursors;

            CZ_DEBUG_ASSERT(commit.edits.len == cursors.len);
            buffer->undo();
            window->update_cursors(buffer);

            Transaction transaction;
            transaction.init(buffer);
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
            transaction.commit(source.client, command_delete_forward_char);
            return;
        }
    }

    DELETE_FORWARD(forward_char, command_delete_forward_char);
}

REGISTER_COMMAND(command_delete_backward_word);
void command_delete_backward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    DELETE_BACKWARD(backward_word, nullptr);
}

REGISTER_COMMAND(command_delete_forward_word);
void command_delete_forward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    DELETE_FORWARD(forward_word, nullptr);
}

REGISTER_COMMAND(command_transpose_characters);
void command_transpose_characters(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;
    Transaction transaction;
    transaction.init(buffer);
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

    transaction.commit(source.client);
}

REGISTER_COMMAND(command_transpose_words);
void command_transpose_words(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;
    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    Contents_Iterator it = buffer->contents.start();
    for (size_t c = 0; c < cursors.len; ++c) {
        it.advance_to(cursors[c].point);

        Contents_Iterator start1, end1, start2, end2;

        start1 = it;
        backward_word(&start1);
        end1 = start1;
        forward_word(&end1);
        end2 = end1;
        forward_word(&end2);
        start2 = end2;
        backward_word(&start2);

        // No word to swap.
        if (start1.position == start2.position) {
            continue;
        }

        // Replace the second word.
        Edit delete2;
        delete2.value =
            buffer->contents.slice(transaction.value_allocator(), start2, end2.position);
        delete2.position = start2.position;
        delete2.flags = Edit::REMOVE_AFTER_POSITION;
        transaction.push(delete2);
        Edit insert1;
        insert1.value =
            buffer->contents.slice(transaction.value_allocator(), start1, end1.position);
        insert1.position = start2.position;
        insert1.flags = Edit::INSERT;
        transaction.push(insert1);

        // Replace the first word.
        Edit delete1;
        delete1.value = insert1.value;
        delete1.position = start1.position;
        delete1.flags = Edit::REMOVE_AFTER_POSITION;
        transaction.push(delete1);
        Edit insert2;
        insert2.value = delete2.value;
        insert2.position = start1.position;
        insert2.flags = Edit::INSERT;
        transaction.push(insert2);

        // Put the cursor after the second word.
        cursors[c].point = end2.position;
    }

    transaction.commit(source.client);
}

REGISTER_COMMAND(command_open_line);
void command_open_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    insert_char(source.client, buffer, window, '\n');
    window->update_cursors(buffer);
    TRANSFORM_POINTS(backward_char);
}

REGISTER_COMMAND(command_insert_newline_no_indent);
void command_insert_newline_no_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    insert_char(source.client, buffer, window, '\n');
}

REGISTER_COMMAND(command_duplicate_line);
void command_duplicate_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start;
        Contents_Iterator end;
        if (window->show_marks) {
            window->show_marks = 1;
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

    transaction.commit(source.client);
}

static void command_duplicate_line_prompt_callback(Editor* editor,
                                                   Client* client,
                                                   cz::Str query,
                                                   void* _data) {
    uint64_t num = 0;
    if (cz::parse(query, &num) != (int64_t)query.len) {
        client->show_message("Error: Invalid input");
        return;
    }

    WITH_SELECTED_BUFFER(client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start;
        Contents_Iterator end;
        if (window->show_marks) {
            window->show_marks = 1;
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

        for (size_t n = 0; n < num; ++n) {
            Edit edit;
            edit.value = SSOStr::from_constant({value, region_size});
            edit.position = start.position + offset;
            offset += edit.value.len();
            edit.flags = Edit::INSERT;
            transaction.push(edit);
        }
    }

    transaction.commit(client);
}

REGISTER_COMMAND(command_duplicate_line_prompt);
void command_duplicate_line_prompt(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Times to duplicate: ";
    dialog.response_callback = command_duplicate_line_prompt_callback;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_delete_line);
void command_delete_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
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

    if (offset == 0) {
        return;
    }

    transaction.commit(source.client);
}

REGISTER_COMMAND(command_delete_end_of_line);
void command_delete_end_of_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
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

    if (offset == 0) {
        return;
    }

    transaction.commit(source.client);
}

REGISTER_COMMAND(command_delete_start_of_line_text);
void command_delete_start_of_line_text(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator end = buffer->contents.iterator_at(cursors[c].point);
        Contents_Iterator start = end;
        start_of_line_text(&start);

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(), start, end.position);
        edit.position = start.position - offset;
        offset += edit.value.len();
        edit.flags = Edit::REMOVE;
        transaction.push(edit);
    }

    if (offset == 0) {
        return;
    }

    transaction.commit(source.client);
}

REGISTER_COMMAND(command_delete_start_of_line);
void command_delete_start_of_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator end = buffer->contents.iterator_at(cursors[c].point);
        Contents_Iterator start = end;
        start_of_line(&start);

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(), start, end.position);
        edit.position = start.position - offset;
        offset += edit.value.len();
        edit.flags = Edit::REMOVE;
        transaction.push(edit);
    }

    if (offset == 0) {
        return;
    }

    transaction.commit(source.client);
}

template <class Func>
static void fill_region(Client* client,
                        Buffer* buffer,
                        Window_Unified* window,
                        Func func,
                        cz::Str filler) {
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t max_region_size = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        uint64_t size = 0;
        if (window->show_marks) {
            size = cursors[c].end() - cursors[c].start();
        } else {
            Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
            func(&start);
            if (start.position >= cursors[c].point) {
                continue;
            }
            size = cursors[c].point - start.position;
        }

        if (max_region_size < size) {
            max_region_size = size;
        }
    }

    if (max_region_size == 0) {
        return;
    }

    if (filler.len == 0)
        filler = " ";

    cz::String spaces = {};
    spaces.reserve(transaction.value_allocator(), max_region_size);
    while (spaces.len < max_region_size) {
        if (spaces.len + filler.len <= max_region_size)
            spaces.append(filler);
        else
            spaces.append(filler.slice_end(max_region_size - spaces.len));
    }

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start, end;
        if (window->show_marks) {
            start = end = buffer->contents.iterator_at(cursors[c].end());
            start.retreat_to(cursors[c].start());
        } else {
            start = end = buffer->contents.iterator_at(cursors[c].point);
            func(&start);
            if (start.position >= end.position) {
                continue;
            }
        }

        Edit remove;
        remove.value = buffer->contents.slice(transaction.value_allocator(), start, end.position);
        remove.position = start.position;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value = SSOStr::from_constant(spaces.slice_end(remove.value.len()));
        insert.position = start.position;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit(client);
}

REGISTER_COMMAND(command_fill_region_with_spaces);
void command_fill_region_with_spaces(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!window->show_marks) {
        source.client->show_message("No region selected");
        return;
    }
    fill_region(
        source.client, buffer, window,
        [](Contents_Iterator*) { CZ_PANIC("Buffer mutated while locked"); }, " ");
}

static void command_fill_region_with_prompt_callback(Editor* editor,
                                                     Client* client,
                                                     cz::Str filler,
                                                     void* data) {
    WITH_SELECTED_BUFFER(client);
    if (!window->show_marks) {
        client->show_message("No region selected");
        return;
    }
    fill_region(
        client, buffer, window, [](Contents_Iterator*) { CZ_PANIC("Buffer mutated while locked"); },
        filler);
}

REGISTER_COMMAND(command_fill_region_with_prompt);
void command_fill_region_with_prompt(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Fill with: ";
    dialog.response_callback = command_fill_region_with_prompt_callback;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_fill_region_or_solt_with_spaces);
void command_fill_region_or_solt_with_spaces(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    fill_region(source.client, buffer, window, start_of_line_text, " ");
}

REGISTER_COMMAND(command_undo);
void command_undo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!buffer->undo()) {
        source.client->show_message("Nothing to undo");
        return;
    }

    if (window->cursors.len == 1) {
        uint64_t position = buffer->changes.last().commit.edits[0].position;
        Contents_Iterator iterator = buffer->contents.iterator_at(position);
        if (!is_visible(window, buffer->mode, editor->theme, iterator)) {
            window->cursors[0].point = position;
            center_in_window(window, buffer->mode, editor->theme, iterator);
        }
    }
}

REGISTER_COMMAND(command_redo);
void command_redo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!buffer->redo()) {
        source.client->show_message("Nothing to redo");
        return;
    }

    if (window->cursors.len == 1) {
        uint64_t position = buffer->changes.last().commit.edits[0].position;
        Contents_Iterator iterator = buffer->contents.iterator_at(position);
        if (!is_visible(window, buffer->mode, editor->theme, iterator)) {
            window->cursors[0].point = position;
            center_in_window(window, buffer->mode, editor->theme, iterator);
        }
    }
}

REGISTER_COMMAND(command_stop_action);
void command_stop_action(Editor* editor, Command_Source source) {
    const char* message = nullptr;

    Window_Unified* window = source.client->selected_window();
    if (!message && window->completing) {
        window->abort_completion();
        message = "Stop completion";
    }

    if (!message && window->show_marks) {
        window->show_marks = false;
        message = "Stop selecting region";
    }

    if (!message && window->cursors.len > 1) {
        kill_extra_cursors(window, source.client);
        message = "Stop multiple cursors";
    }

    if (!message && source.client->_select_mini_buffer) {
        source.client->hide_mini_buffer(editor);
        message = "Stop prompting";
    }

    if (!message) {
        message = "Nothing to stop";
    }

    source.client->show_message(message);
}

REGISTER_COMMAND(command_quit);
void command_quit(Editor* editor, Command_Source source) {
    source.client->queue_quit = true;
}

static void command_goto_line_callback(Editor* editor, Client* client, cz::Str str, void* data) {
    uint64_t lines = 0;
    cz::parse(str, &lines);

    WITH_CONST_SELECTED_BUFFER(client);
    if (window->cursors.len > 1 ||
        (window->cursors[0].point != 0 && window->cursors[0].point != buffer->contents.len)) {
        push_jump(window, client, buffer);
    }

    Contents_Iterator iterator = start_of_line_position(buffer->contents, lines);
    window->cursors[0].point = iterator.position;
    center_in_window(window, buffer->mode, editor->theme, iterator);
}

static void command_goto_position_callback(Editor* editor,
                                           Client* client,
                                           cz::Str str,
                                           void* data) {
    uint64_t position = 0;
    cz::parse(str, &position);

    WITH_CONST_SELECTED_BUFFER(client);
    if (window->cursors.len > 1 ||
        (window->cursors[0].point != 0 && window->cursors[0].point != buffer->contents.len)) {
        push_jump(window, client, buffer);
    }

    Contents_Iterator iterator =
        buffer->contents.iterator_at(cz::min(position, buffer->contents.len));
    window->cursors[0].point = iterator.position;
    center_in_window(window, buffer->mode, editor->theme, iterator);
}

static void command_goto_column_callback(Editor* editor, Client* client, cz::Str str, void* data) {
    uint64_t column = 0;
    cz::parse(str, &column);

    WITH_CONST_SELECTED_BUFFER(client);
    Contents_Iterator iterator = buffer->contents.start();
    for (size_t i = 0; i < window->cursors.len; ++i) {
        iterator.go_to(window->cursors[i].point);
        go_to_visual_column(buffer->mode, &iterator, column);
        window->cursors[i].point = iterator.position;
    }
}

REGISTER_COMMAND(command_goto_line);
void command_goto_line(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Goto line: ";
    dialog.response_callback = command_goto_line_callback;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_goto_position);
void command_goto_position(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Goto position: ";
    dialog.response_callback = command_goto_position_callback;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_goto_column);
void command_goto_column(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Goto column: ";
    dialog.response_callback = command_goto_column_callback;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_show_file_length_info);
void command_show_file_length_info(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    uint64_t start = 0;
    uint64_t end = buffer->contents.len;
    if (window->cursors.len == 1 && window->show_marks) {
        start = window->cursors[0].start();
        end = window->cursors[0].end();
    }

    uint64_t lines =
        buffer->contents.get_line_number(end) - buffer->contents.get_line_number(start);

    uint64_t words = 0;
    Contents_Iterator words_it = buffer->contents.iterator_at(start);
    while (words_it.position < end) {
        ++words;

        forward_word(&words_it);

        // Fix word at end of file not being counted.
        if (words_it.position >= end) {  // end of region so >=
            words_it.retreat();
            if (cz::is_alnum(words_it.get())) {
                backward_word(&words_it);
                if (words_it.position < end) {  // beginning of region so <
                    ++words;
                }
            }
            break;
        }
    }

    if (words > 0)
        --words;

    cz::String string = {};
    CZ_DEFER(string.drop(cz::heap_allocator()));

    cz::append(cz::heap_allocator(), &string, "Bytes: ", end - start, ", lines: ", lines,
               ", words: ", words,
               ", cursor position: ", window->cursors[window->selected_cursor].point);

    source.client->show_message(string);
}

REGISTER_COMMAND(command_path_up_directory);
void command_path_up_directory(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (buffer->contents.len == 0) {
        return;
    }

    Contents_Iterator start = buffer->contents.iterator_at(buffer->contents.len);
    start.retreat();

    // If we find a '/' then delete after it.  Otherwise delete the entire buffer.
    if (rfind(&start, '/')) {
        start.advance();
    }

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    Edit edit;
    edit.value = buffer->contents.slice(transaction.value_allocator(), start, buffer->contents.len);
    edit.position = start.position;
    edit.flags = Edit::REMOVE;
    transaction.push(edit);

    transaction.commit(source.client);
}

REGISTER_COMMAND(command_mark_buffer);
void command_mark_buffer(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    if (window->cursors.len > 1 ||
        (window->cursors[0].point != 0 && window->cursors[0].point != buffer->contents.len)) {
        push_jump(window, source.client, buffer);
    }
    window->show_marks = true;
    window->cursors[0].mark = 0;
    window->cursors[0].point = buffer->contents.len;
}

void submit_mini_buffer(Editor* editor, Client* client) {
    SSOStr mini_buffer_contents;
    {
        Window_Unified* window = client->mini_buffer_window();
        WITH_WINDOW_BUFFER(window);
        mini_buffer_contents = clear_buffer(client, buffer);
    }

    {
        WITH_BUFFER_HANDLE(client->messages_buffer_handle);
        buffer->contents.insert(client->_message.end, mini_buffer_contents.as_str());
    }

    client->restore_selected_buffer();

    Message message = client->_message;
    CZ_DEFER(cz::heap_allocator().dealloc({message.response_callback_data, 0}));
    client->_message.response_callback_data = nullptr;
    client->dealloc_message();

    message.response_callback(editor, client, mini_buffer_contents.as_str(),
                              message.response_callback_data);
}

REGISTER_COMMAND(command_submit_mini_buffer);
void command_submit_mini_buffer(Editor* editor, Command_Source source) {
    submit_mini_buffer(editor, source.client);
}

REGISTER_COMMAND(command_insert_home_directory);
void command_insert_home_directory(Editor* editor, Command_Source source) {
    if (!user_home_path) {
        return;
    }

    cz::Str str = user_home_path;
    size_t extra = 0;
    if (!str.ends_with("/") && !str.ends_with("\\")) {
        extra = 1;
    }

    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    char* buf = (char*)transaction.value_allocator().alloc({str.len + extra, 1});
    memcpy(buf, str.buffer, str.len);
    if (extra) {
        buf[str.len] = '/';
    }

    cz::path::convert_to_forward_slashes(buf, str.len);

    SSOStr ssostr = SSOStr::from_constant({buf, str.len + extra});

    uint64_t offset = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        Edit insert;
        insert.value = ssostr;
        insert.position = cursors[c].point + offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
        offset += str.len;
    }

    transaction.commit(source.client);
}

REGISTER_COMMAND(command_increase_font_size);
void command_increase_font_size(Editor* editor, Command_Source source) {
    editor->theme.font_size += 2;
}

REGISTER_COMMAND(command_decrease_font_size);
void command_decrease_font_size(Editor* editor, Command_Source source) {
    if (editor->theme.font_size >= 2) {
        editor->theme.font_size -= 2;
    }
}

REGISTER_COMMAND(command_show_date_of_build);
void command_show_date_of_build(Editor* editor, Command_Source source) {
    char buffer[20];
    format_date(program_date, buffer);

    cz::Heap_String message = cz::format("Date of build: ", buffer);
    CZ_DEFER(message.drop());
    source.client->show_message(message);
}

REGISTER_COMMAND(command_comment_hash);
void command_comment_hash(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    generic_line_comment(source.client, buffer, window, "#", /*add=*/true);
}

REGISTER_COMMAND(command_uncomment_hash);
void command_uncomment_hash(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    generic_line_comment(source.client, buffer, window, "#", /*add=*/false);
}

static void slice_and_add_line(Contents_Iterator sol,
                               cz::Vector<SSOStr>* lines,
                               cz::Allocator allocator) {
    Contents_Iterator eol = sol;
    start_of_line(&sol);
    end_of_line(&eol);
    if (sol.position == eol.position) {
        return;
    }

    SSOStr string = sol.contents->slice(allocator, sol, eol.position);
    lines->push(string);
}

static void make_sorted_lines(cz::Vector<SSOStr>* sorted_lines,
                              cz::Slice<SSOStr> unsorted_lines,
                              int order) {
    sorted_lines->reserve(cz::heap_allocator(), unsorted_lines.len);
    sorted_lines->append(unsorted_lines);

    if (order == 0) {  // Ascending
        cz::sort(*sorted_lines,
                 [](SSOStr* left, SSOStr* right) { return left->as_str() < right->as_str(); });
    } else if (order == 1) {  // Descending
        cz::sort(*sorted_lines,
                 [](SSOStr* left, SSOStr* right) { return left->as_str() > right->as_str(); });
    } else if (order == 2) {  // Ascending shortlex
        cz::sort(*sorted_lines, [](SSOStr* left, SSOStr* right) {
            return cz::Shortlex{}.compare(left->as_str(), right->as_str()) < 0;
        });
    } else if (order == 3) {  // Descending shortlex
        cz::sort(*sorted_lines, [](SSOStr* left, SSOStr* right) {
            return cz::Shortlex{}.compare(left->as_str(), right->as_str()) > 0;
        });
    } else {  // Flip
        for (size_t i = 0; i < sorted_lines->len / 2; ++i) {
            cz::swap(sorted_lines->get(i), sorted_lines->get(sorted_lines->len - 1 - i));
        }
    }
}

static void set_line_to_sorted(SSOStr sorted_line,
                               SSOStr unsorted_line,
                               uint64_t position,
                               uint64_t* offset,
                               Transaction* transaction) {
    // Don't create unnecessary edits.
    if (sorted_line.as_str() == unsorted_line.as_str()) {
        return;
    }

    Edit remove_old_line;
    remove_old_line.value = unsorted_line;
    remove_old_line.position = position + *offset;
    remove_old_line.flags = Edit::REMOVE_AFTER_POSITION;
    transaction->push(remove_old_line);

    Edit insert_new_line;
    insert_new_line.value = sorted_line;
    insert_new_line.position = position + *offset;
    insert_new_line.flags = Edit::INSERT_AFTER_POSITION;
    transaction->push(insert_new_line);

    *offset += insert_new_line.value.len() - remove_old_line.value.len();
}

static void sort_lines(Client* client, Buffer* buffer, Window_Unified* window, int order) {
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Vector<SSOStr> unsorted_lines = {};
    CZ_DEFER(unsorted_lines.drop(cz::heap_allocator()));

    cz::Vector<SSOStr> sorted_lines = {};
    CZ_DEFER(sorted_lines.drop(cz::heap_allocator()));

    Contents_Iterator it = buffer->contents.start();
    if (window->show_marks) {
        for (size_t i = 0; i < cursors.len; ++i) {
            unsorted_lines.len = 0;
            sorted_lines.len = 0;

            it.advance_to(cursors[i].start());
            start_of_line(&it);
            while (it.position < cursors[i].end()) {
                unsorted_lines.reserve(cz::heap_allocator(), 1);
                slice_and_add_line(it, &unsorted_lines, transaction.value_allocator());

                end_of_line(&it);
                forward_char(&it);
            }

            make_sorted_lines(&sorted_lines, unsorted_lines, order);

            it.retreat_to(cursors[i].start());
            start_of_line(&it);

            uint64_t offset = 0;
            size_t line = 0;
            while (it.position < cursors[i].end()) {
                if (!at_end_of_line(it)) {
                    set_line_to_sorted(sorted_lines[line], unsorted_lines[line], it.position,
                                       &offset, &transaction);
                    ++line;
                }

                end_of_line(&it);
                forward_char(&it);
            }
        }
    } else {
        unsorted_lines.reserve(cz::heap_allocator(), cursors.len);

        for (size_t i = 0; i < cursors.len; ++i) {
            it.advance_to(cursors[i].point);
            slice_and_add_line(it, &unsorted_lines, transaction.value_allocator());
        }

        make_sorted_lines(&sorted_lines, unsorted_lines, order);

        it.retreat_to(cursors[0].point);

        uint64_t offset = 0;
        size_t line = 0;
        for (size_t i = 0; i < cursors.len; ++i) {
            it.advance_to(cursors[i].point);
            start_of_line(&it);
            if (!at_end_of_line(it)) {
                set_line_to_sorted(sorted_lines[line], unsorted_lines[line], it.position, &offset,
                                   &transaction);
                ++line;
            }
        }
    }

    transaction.commit(client);
}

REGISTER_COMMAND(command_sort_lines_ascending);
void command_sort_lines_ascending(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    sort_lines(source.client, buffer, window, 0);
}

REGISTER_COMMAND(command_sort_lines_descending);
void command_sort_lines_descending(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    sort_lines(source.client, buffer, window, 1);
}

REGISTER_COMMAND(command_sort_lines_ascending_shortlex);
void command_sort_lines_ascending_shortlex(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    sort_lines(source.client, buffer, window, 2);
}

REGISTER_COMMAND(command_sort_lines_descending_shortlex);
void command_sort_lines_descending_shortlex(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    sort_lines(source.client, buffer, window, 3);
}

REGISTER_COMMAND(command_flip_lines);
void command_flip_lines(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    sort_lines(source.client, buffer, window, 4);
}

REGISTER_COMMAND(command_deduplicate_lines);
void command_deduplicate_lines(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    Contents_Iterator it = buffer->contents.start();

    cz::String prev = {}, next = {};
    CZ_DEFER(prev.drop(cz::heap_allocator()));
    CZ_DEFER(next.drop(cz::heap_allocator()));

    uint64_t offset = 0;

    if (window->show_marks) {
        for (size_t i = 0; i < window->cursors.len; ++i) {
            it.advance_to(window->cursors[i].start());
            start_of_line(&it);

            prev.len = 0;

            bool first = true;
            while (it.position < window->cursors[i].end()) {
                Contents_Iterator eol = it;
                end_of_line(&eol);

                next.len = 0;
                buffer->contents.slice_into(cz::heap_allocator(), it, eol.position, &next);

                if (first) {
                    first = false;
                } else if (prev == next) {
                    Edit remove;
                    it.retreat();  // Delete the newline from the previous line.
                    remove.value =
                        buffer->contents.slice(transaction.value_allocator(), it, eol.position);
                    remove.position = it.position - offset;
                    remove.flags = Edit::REMOVE;
                    transaction.push(remove);

                    offset += remove.value.len();
                }

                cz::swap(prev, next);

                it = eol;
                forward_char(&it);
            }
        }
    } else {
        bool first = true;
        uint64_t prev_position = 0;
        for (size_t i = 0; i < window->cursors.len; ++i) {
            it.advance_to(window->cursors[i].point);
            start_of_line(&it);

            // Don't consider a line as a duplicate with itself.
            if (!first && it.position == prev_position)
                continue;
            prev_position = it.position;

            Contents_Iterator eol = it;
            end_of_line(&eol);

            next.len = 0;
            buffer->contents.slice_into(cz::heap_allocator(), it, eol.position, &next);

            if (first) {
                first = false;
            } else if (prev == next) {
                Edit remove;
                it.retreat();  // Delete the newline from the previous line.
                remove.value =
                    buffer->contents.slice(transaction.value_allocator(), it, eol.position);
                remove.position = it.position - offset;
                remove.flags = Edit::REMOVE;
                transaction.push(remove);

                offset += remove.value.len();
                window->cursors.remove(i);
                if (window->selected_cursor >= i)
                    --window->selected_cursor;
                --i;
            }

            cz::swap(prev, next);

            it = eol;
            forward_char(&it);
        }
    }

    transaction.commit(source.client);
}

REGISTER_COMMAND(command_restore_last_save_point);
void command_restore_last_save_point(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (!buffer->saved_commit_id.is_present) {
        // Undo all edits.
        while (buffer->undo()) {
        }
        return;
    }

    Commit_Id saved_commit_id = buffer->saved_commit_id.value;
    for (size_t i = 0; i < buffer->commits.len; ++i) {
        if (buffer->commits[i].id == saved_commit_id) {
            while (i < buffer->commit_index) {
                if (!buffer->undo()) {
                    return;
                }
            }
            while (i >= buffer->commit_index) {
                if (!buffer->redo()) {
                    return;
                }
            }
            return;
        }
    }
}

REGISTER_COMMAND(command_undo_all);
void command_undo_all(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    while (buffer->undo()) {
    }
}

REGISTER_COMMAND(command_redo_all);
void command_redo_all(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    while (buffer->redo()) {
    }
}

static void command_run_command_for_result_callback(Editor* editor,
                                                    Client* client,
                                                    cz::Str script,
                                                    void*) {
    cz::String buffer_name = {};
    CZ_DEFER(buffer_name.drop(cz::heap_allocator()));
    buffer_name.reserve(cz::heap_allocator(), 6 + script.len);
    buffer_name.append("shell ");
    buffer_name.append(script);

    client->close_fused_paired_windows();

    WITH_CONST_SELECTED_BUFFER(client);

    run_console_command(client, editor, buffer->directory.buffer, script, buffer_name,
                        "Shell error");
}

static void command_run_command_ignore_result_callback(Editor* editor,
                                                       Client* client,
                                                       cz::Str script,
                                                       void*) {
    WITH_CONST_SELECTED_BUFFER(client);

    cz::Process_Options options;
    options.working_directory = buffer->directory.buffer;

    cz::Process process;
    if (!process.launch_script(script, options)) {
        client->show_message("Shell error");
        return;
    }

    editor->add_asynchronous_job(job_process_silent(process));
}

REGISTER_COMMAND(command_run_command_for_result);
void command_run_command_for_result(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Shell buffer: ";
    dialog.response_callback = command_run_command_for_result_callback;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_run_command_ignore_result);
void command_run_command_ignore_result(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Shell silent: ";
    dialog.response_callback = command_run_command_ignore_result_callback;
    source.client->show_dialog(dialog);
}

static void command_replace_region_callback(Editor* editor,
                                            Client* client,
                                            cz::Str replacement,
                                            void*) {
    if (replacement.len != 1) {
        client->show_message("Error: must be a single character replacement");
        return;
    }

    WITH_SELECTED_BUFFER(client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    Contents_Iterator it = buffer->contents.start();

    for (size_t i = 0; i < window->cursors.len; ++i) {
        uint64_t start = window->cursors[i].start();
        uint64_t end = window->cursors[i].end();
        if (i + 1 < window->cursors.len)
            end = cz::min(end, window->cursors[i + 1].start());
        it.advance_to(start);

        Edit remove;
        remove.value = buffer->contents.slice(transaction.value_allocator(), it, end);
        remove.position = start;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        SSOStr value = remove.value.clone(transaction.value_allocator());
        memset((char*)value.buffer(), replacement[0], value.len());

        Edit insert;
        insert.value = value;
        insert.position = start;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit(client);
}

REGISTER_COMMAND(command_replace_region);
void command_replace_region(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Replace region with character: ";
    dialog.response_callback = command_replace_region_callback;
    source.client->show_dialog(dialog);
}

static void command_insert_num_callback(Editor* editor,
                                        Client* client,
                                        cz::Str replacement,
                                        void*) {
    size_t num;
    cz::Str string;
    if (cz::parse(replacement, &num, ' ', cz::rest(&string)) != (int64_t)replacement.len) {
        client->show_message("Error: invalid input; must match format 'NUM STRING'.");
        return;
    }

    WITH_SELECTED_BUFFER(client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    // Build the string to be inserted.
    cz::String builder = {};
    builder.reserve_exact(transaction.value_allocator(), num * string.len);
    for (size_t i = 0; i < num; ++i)
        builder.append(string);

    SSOStr to_insert = SSOStr::from_constant(builder);

    for (size_t i = 0; i < window->cursors.len; ++i) {
        Edit insert;
        insert.value = to_insert;
        insert.position = window->cursors[i].point + i * to_insert.len();
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit(client);
}

REGISTER_COMMAND(command_insert_num);
void command_insert_num(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Insert repeated.  Use format 'NUM STRING': ";
    dialog.response_callback = command_insert_num_callback;
    source.client->show_dialog(dialog);
}

void insert_divider_helper(Editor* editor, Command_Source source, char ch, uint64_t target_column) {
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.start();
    uint64_t min_column = -1;
    for (size_t c = 0; c < window->cursors.len; ++c) {
        it.advance_to(window->cursors[c].point);
        min_column = cz::min(min_column, get_visual_column(buffer->mode, it));
    }

    Transaction transaction = {};
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::String divider = {};
    divider.reserve_exact(transaction.value_allocator(), target_column - min_column);
    divider.push_many(ch, target_column - min_column);

    uint64_t offset = 0;
    for (size_t c = 0; c < window->cursors.len; ++c) {
        it.advance_to(window->cursors[c].point);
        uint64_t column = get_visual_column(buffer->mode, it);
        if (column >= target_column)
            continue;

        Edit insert;
        insert.value = SSOStr::from_constant(divider.slice_end(target_column - column));
        insert.position = it.position + offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
        offset += insert.value.len();
    }

    transaction.commit(source.client);
}

void insert_header_helper(Editor* editor,
                          Command_Source source,
                          char ch,
                          uint64_t min_num,
                          uint64_t target_column) {
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.start();
    uint64_t min_column = -1;
    for (size_t c = 0; c < window->cursors.len; ++c) {
        it.advance_to(window->cursors[c].point);
        min_column = cz::min(min_column, get_visual_column(buffer->mode, it));
    }

    Transaction transaction = {};
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::String divider = {};
    divider.reserve_exact(transaction.value_allocator(), target_column - min_column + 1);
    divider.push_many(ch, target_column - min_column);
    divider.push('\n');

    cz::String starter = {};
    starter.reserve_exact(transaction.value_allocator(), min_num + 2);
    starter.push_many(ch, min_num);
    starter.push(' ');
    starter.push('\n');

    cz::Vector<uint64_t> cursor_outputs = {};
    CZ_DEFER(cursor_outputs.drop(cz::heap_allocator()));
    cursor_outputs.reserve_exact(cz::heap_allocator(), window->cursors.len);

    uint64_t offset = 0;
    for (size_t c = 0; c < window->cursors.len; ++c) {
        it.advance_to(window->cursors[c].point);
        uint64_t column = get_visual_column(buffer->mode, it);
        if (column >= target_column)
            continue;

        Edit top;
        top.value =
            SSOStr::from_constant(divider.slice_start(divider.len - 1 - (target_column - column)));
        top.position = it.position + offset;
        top.flags = Edit::INSERT;
        transaction.push(top);
        offset += top.value.len();

        Contents_Iterator sol = it;
        start_of_line(&sol);
        SSOStr indent = buffer->contents.slice(transaction.value_allocator(), sol, it.position);

        Edit indent_middle;
        indent_middle.value = indent;
        indent_middle.position = it.position + offset;
        indent_middle.flags = Edit::INSERT;
        transaction.push(indent_middle);
        offset += indent_middle.value.len();

        Edit middle;
        middle.value = SSOStr::from_constant(starter);
        middle.position = it.position + offset;
        middle.flags = Edit::INSERT;
        transaction.push(middle);
        offset += middle.value.len();

        cursor_outputs.push(window->cursors[c].point + offset - 1);

        Edit indent_bottom;
        indent_bottom.value = indent;
        indent_bottom.position = it.position + offset;
        indent_bottom.flags = Edit::INSERT;
        transaction.push(indent_bottom);
        offset += indent_bottom.value.len();

        Edit bottom;
        // Don't include the newline in the bottom edit.
        bottom.value = SSOStr::from_constant(divider.slice_end(target_column - column));
        bottom.position = it.position + offset;
        bottom.flags = Edit::INSERT;
        transaction.push(bottom);
        offset += bottom.value.len();
    }

    if (!transaction.commit(source.client))
        return;

    window->update_cursors(buffer);
    for (size_t c = 0; c < window->cursors.len; ++c) {
        window->cursors[c].point = cursor_outputs[c];
    }
}

}
}
