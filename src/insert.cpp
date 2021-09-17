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
    for (size_t i = 0; i < cursors.len; ++i) {
        Edit edit;
        edit.value = value;
        edit.position = cursors[i].point + i;
        edit.flags = Edit::INSERT;
        transaction.push(edit);
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

void command_insert_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    do_command_insert_char(editor, buffer, window, source);
}

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
    if (buffer->mode.use_tabs && code == ' ' && buffer->mode.tab_width > 0) {
        // See if there are any cursors we want to merge at.
        cz::Slice<Cursor> cursors = window->cursors;
        Contents_Iterator it = buffer->contents.start();
        size_t merge_tab = 0;
        for (size_t e = 0; e < cursors.len; ++e) {
            // No space before so we can't merge.
            if (cursors[e].point == 0) {
                continue;
            }

            it.advance_to(cursors[e].point);

            uint64_t column = get_visual_column(buffer->mode, it);

            // If the character before is not a space we can't merge.
            it.retreat();
            if (it.get() != ' ') {
                continue;
            }

            uint64_t end = it.position + 1;
            while (!it.at_bob() && end - it.position + 1 < buffer->mode.tab_width) {
                it.retreat();
                if (it.get() != ' ') {
                    it.advance();
                    break;
                }
            }

            // And if we're not going to hit a tab level we can't use a tab.
            if ((column + 1) % buffer->mode.tab_width == 0 &&
                end - it.position + 1 == buffer->mode.tab_width) {
                ++merge_tab;
            }
        }

        if (merge_tab > 0) {
            Transaction transaction;
            transaction.init(buffer);
            CZ_DEFER(transaction.drop());

            char* buf = (char*)transaction.value_allocator().alloc({buffer->mode.tab_width - 1, 1});
            memset(buf, ' ', buffer->mode.tab_width - 1);
            SSOStr spaces = SSOStr::from_constant({buf, buffer->mode.tab_width - 1});

            int64_t offset = 0;
            if (cursors.len > 1) {
                it.retreat_to(cursors[0].point);
            }
            for (size_t c = 0; c < cursors.len; ++c) {
                it.advance_to(cursors[c].point);

                if (it.position > 0) {
                    uint64_t column = get_visual_column(buffer->mode, it);

                    uint64_t end = it.position;
                    it.retreat();

                    // If the character before is not a space we can't merge.
                    // And if we're not going to hit a tab level we can't use a tab.
                    if (it.get() == ' ' && (column + 1) % buffer->mode.tab_width == 0) {
                        while (!it.at_bob()) {
                            it.retreat();
                            if (it.get() != ' ') {
                                it.advance();
                                break;
                            }
                        }

                        if (end - it.position + 1 == buffer->mode.tab_width) {
                            // Remove the spaces.
                            Edit remove;
                            remove.position = it.position + offset;
                            remove.value = spaces;
                            remove.flags = Edit::REMOVE;
                            transaction.push(remove);

                            // Insert the character.
                            Edit insert;
                            insert.position = it.position + offset;
                            insert.value = SSOStr::from_char('\t');
                            insert.flags = Edit::INSERT;
                            transaction.push(insert);

                            offset -= end - it.position;
                            ++offset;
                            continue;
                        }
                    }

                    // Reset if we arent replacing with a tab.
                    it.advance_to(end);
                }

                // Insert the character.
                Edit insert;
                insert.position = it.position + offset;
                insert.value = SSOStr::from_char(code);
                insert.flags = Edit::INSERT;
                transaction.push(insert);

                ++offset;
            }

            // Don't merge edits around tab replacement.
            transaction.commit(source.client);
            return;
        }
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

}
