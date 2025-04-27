#include "insert.hpp"

#include <cz/defer.hpp>
#include "buffer.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "movement.hpp"
#include "ssostr.hpp"
#include "transaction.hpp"
#include "window.hpp"

namespace mag {

void insert(Client* client,
            Buffer* buffer,
            Window_Unified* window,
            SSOStr value,
            Command_Function committer) {
    ZoneScoped;

    window->update_cursors(buffer);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Slice<Cursor> cursors = window->cursors;
    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        Edit edit;
        edit.value = value;
        edit.position = cursors[i].point + offset;
        edit.flags = Edit::INSERT;
        transaction.push(edit);
        offset += value.len();
    }

    transaction.commit(client, committer);
}

void insert_char(Client* client,
                 Buffer* buffer,
                 Window_Unified* window,
                 char code,
                 Command_Function committer) {
    insert(client, buffer, window, SSOStr::from_char(code), committer);
}

void delete_regions(Client* client,
                    Buffer* buffer,
                    Window_Unified* window,
                    Command_Function committer) {
    ZoneScoped;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t i = 0; i < cursors.len; ++i) {
        uint64_t start = cursors[i].start();
        uint64_t end = cursors[i].end();

        if (end == start)
            continue;

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(),
                                            buffer->contents.iterator_at(start), end);
        edit.position = start - offset;
        offset += end - start;
        edit.flags = Edit::REMOVE;
        transaction.push(edit);
    }

    transaction.commit(client, committer);
}

static bool is_word_char(char c) {
    return cz::is_alnum(c) || c == '_';
}
static bool can_merge_insert(cz::Str str, char code) {
    char last = str[str.len - 1];
    return last == code || (is_word_char(last) && is_word_char(code));
}

REGISTER_COMMAND(command_insert_char);
void command_insert_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (source.keys.len == 0) {
        source.client->show_message("command_insert_char must be called via keybind");
        return;
    }

    do_command_insert_char(editor, buffer, window, source);
}

static bool try_merge_spaces_into_tabs(Window_Unified* window, Buffer* buffer, Client* client);

void do_command_insert_char(Editor* editor,
                            Buffer* buffer,
                            Window_Unified* window,
                            Command_Source source) {
    CZ_ASSERT(source.keys[0].code <= UCHAR_MAX);

    char code = (char)source.keys[0].code;

    // If temporarily showing marks then first delete the region then
    // type.  This makes 2 edits so you can redo inbetween these edits.
    if (window->show_marks == 2) {
        delete_regions(source.client, buffer, window);
        insert_char(source.client, buffer, window, code, command_insert_char);
        window->show_marks = 0;
        return;
    }

    if (editor->theme.insert_replace) {
        Transaction transaction;
        transaction.init(buffer);
        CZ_DEFER(transaction.drop());

        cz::Slice<Cursor> cursors = window->cursors;
        Contents_Iterator it = buffer->contents.start();
        uint64_t offset = 0;
        for (size_t c = 0; c < cursors.len; ++c) {
            it.advance_to(cursors[c].point);

            // The goal of insert mode is to keep text after our column at the same column.
            bool should_remove = false;
            if (!it.at_eob()) {
                if (it.get() == '\n') {
                    // No text after our column so don't delete the newline.
                    should_remove = false;
                } else if (it.get() == '\t') {
                    // Tabs normally can collapse but if they are
                    // currently 1 column wide then it can't.
                    uint64_t start_column = get_visual_column(buffer->mode, it);
                    uint64_t end_column = char_visual_columns(buffer->mode, '\t', start_column);
                    should_remove = (start_column + 1 == end_column);
                } else {
                    // Replace other text.
                    should_remove = true;
                }

                if (should_remove) {
                    Edit remove;
                    remove.position = it.position + offset;
                    remove.value =
                        buffer->contents.slice(transaction.value_allocator(), it, it.position + 1);
                    remove.flags = Edit::REMOVE;
                    transaction.push(remove);
                }
            }

            // Insert the character.
            Edit insert;
            insert.position = it.position + offset;
            insert.value = SSOStr::from_char(code);
            insert.flags = Edit::INSERT;
            transaction.push(insert);

            if (!should_remove)
                ++offset;
        }

        transaction.commit(source.client);
        return;
    }

    // Merge spaces into tabs.
    if (code == ' ') {
        CZ_ASSERT(buffer->mode.tab_width > 0);
        if (try_merge_spaces_into_tabs(window, buffer, source.client))
            return;
    }

    if (source.previous_command.function == command_insert_char &&
        buffer->check_last_committer(command_insert_char, window->cursors)) {
        CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len);
        Commit commit = buffer->commits[buffer->commit_index - 1];
        size_t len = commit.edits[0].value.len();
        if (len < SSOStr::MAX_SHORT_LEN && can_merge_insert(commit.edits[0].value.as_str(), code)) {
            CZ_DEBUG_ASSERT(commit.edits.len == window->cursors.len);
            buffer->undo();
            // We don't need to update cursors here because insertion doesn't care.

            Transaction transaction;
            transaction.init(buffer);
            CZ_DEFER(transaction.drop());

            for (size_t e = 0; e < commit.edits.len; ++e) {
                CZ_DEBUG_ASSERT(commit.edits[e].value.is_short());
                CZ_DEBUG_ASSERT(commit.edits[e].value.len() == len);

                Edit edit;
                memcpy(edit.value.short_._buffer, commit.edits[e].value.short_._buffer, len);
                edit.value.short_._buffer[len] = code;
                edit.value.short_.set_len(len + 1);
                edit.position = commit.edits[e].position + e;
                edit.flags = Edit::INSERT;
                transaction.push(edit);
            }

            transaction.commit(source.client, command_insert_char);

            return;
        }
    }

    insert_char(source.client, buffer, window, code, command_insert_char);
}

static bool should_merge_tab_at(const Mode& mode, Contents_Iterator it);

static bool try_merge_spaces_into_tabs(Window_Unified* window, Buffer* buffer, Client* client) {
    // See if there are any cursors we want to merge at.
    cz::Slice<Cursor> cursors = window->cursors;
    Contents_Iterator it = buffer->contents.start();

    bool any_merge = false;
    for (size_t e = 0; e < cursors.len; ++e) {
        it.advance_to(cursors[e].point);
        if (should_merge_tab_at(buffer->mode, it)) {
            any_merge = true;
            break;
        }
    }
    if (!any_merge)
        return false;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::String spaces_buffer = {};
    spaces_buffer.reserve_exact(transaction.value_allocator(), buffer->mode.tab_width - 1);
    spaces_buffer.push_many(' ', buffer->mode.tab_width - 1);
    SSOStr spaces = SSOStr::from_constant(spaces_buffer);
    if (spaces.is_short())
        spaces_buffer.drop(transaction.value_allocator());

    int64_t offset = 0;
    if (cursors.len > 1) {
        it.retreat_to(cursors[0].point);
    }
    for (size_t c = 0; c < cursors.len; ++c) {
        it.advance_to(cursors[c].point);
        if (should_merge_tab_at(buffer->mode, it)) {
            Contents_Iterator start_spaces = it;
            while (!start_spaces.at_bob() &&
                   it.position - start_spaces.position <= buffer->mode.tab_width - 1) {
                start_spaces.retreat();
                if (start_spaces.get() != ' ') {
                    start_spaces.advance();
                    break;
                }
            }

            uint64_t spaces_to_remove = it.position - start_spaces.position;

            // Remove the spaces.
            Edit remove;
            remove.position = start_spaces.position + offset;
            remove.value = SSOStr::from_constant(spaces.as_str().slice_end(spaces_to_remove));
            remove.flags = Edit::REMOVE;
            transaction.push(remove);

            // Insert the character.
            Edit insert;
            insert.position = start_spaces.position + offset;
            insert.value = SSOStr::from_char('\t');
            insert.flags = Edit::INSERT;
            transaction.push(insert);

            offset -= spaces_to_remove;
            ++offset;
        } else {
            // Insert the character.
            Edit insert;
            insert.position = it.position + offset;
            insert.value = SSOStr::from_char(' ');
            insert.flags = Edit::INSERT;
            transaction.push(insert);
            ++offset;
        }
    }

    // Don't merge edits around tab replacement.
    transaction.commit(client);
    return true;
}

static bool should_merge_tab_at(const Mode& mode, Contents_Iterator it) {
    Contents_Iterator sol = it;
    backward_through_whitespace(&sol);
    if (at_start_of_line(sol)) {
        // use_tabs controls tabs for indentation.
        if (!mode.use_tabs)
            return false;
    } else {
        // tabs_for_alignment controls tabs for alignment.
        if (!mode.tabs_for_alignment)
            return false;
    }

    // Must be aligned to the tab width after adding the space.
    uint64_t column = get_visual_column(mode, it);
    if ((column + 1) % mode.tab_width != 0)
        return false;

    // We shouldn't put a tab after spaces so test if there are > tab_width spaces.
    if (it.position > mode.tab_width - 1) {
        Contents_Iterator start_spaces = it;
        while (it.position - start_spaces.position <= mode.tab_width - 1) {
            start_spaces.retreat();
            if (start_spaces.get() != ' ') {
                start_spaces.advance();
                break;
            }
        }
        if (it.position - start_spaces.position > mode.tab_width - 1)
            return false;
    }

    return true;
}

}
