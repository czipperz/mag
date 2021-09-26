#include "indent_commands.hpp"

#include "basic/token_movement_commands.hpp"
#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace basic {

uint64_t remove_spaces(Transaction* transaction,
                       const Buffer* buffer,
                       Contents_Iterator it,
                       uint64_t offset) {
    Contents_Iterator end = it;
    forward_through_whitespace(&end);
    Edit remove;
    remove.value = buffer->contents.slice(transaction->value_allocator(), it, end.position);
    remove.position = it.position + offset;
    remove.flags = Edit::REMOVE;
    transaction->push(remove);
    return end.position - it.position;
}

void insert_line_with_indent(Transaction* transaction,
                             const Mode& mode,
                             uint64_t position,
                             uint64_t* offset,
                             uint64_t columns) {
    uint64_t num_tabs, num_spaces;
    analyze_indent(mode, columns, &num_tabs, &num_spaces);

    char* value = (char*)transaction->value_allocator().alloc({1 + num_tabs + num_spaces, 1});
    value[0] = '\n';
    memset(value + 1, '\t', num_tabs);
    memset(value + 1 + num_tabs, ' ', num_spaces);

    Edit insert;
    insert.value = SSOStr::from_constant({value, 1 + num_tabs + num_spaces});
    insert.position = position + *offset;
    insert.flags = Edit::INSERT;
    transaction->push(insert);

    *offset += insert.value.len();
}

void command_insert_newline_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t i = 0; i < cursors.len; ++i) {
        Contents_Iterator it = buffer->contents.iterator_at(cursors[i].point);
        backward_through_whitespace(&it);

        uint64_t removed = remove_spaces(&transaction, buffer, it, offset);

        uint64_t columns = find_indent_width(buffer, it);
        insert_line_with_indent(&transaction, buffer->mode, it.position, &offset, columns);

        offset -= removed;
    }

    transaction.commit(source.client);
}

void command_insert_newline_copy_indent_and_modifiers(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t i = 0; i < cursors.len; ++i) {
        Contents_Iterator it = buffer->contents.iterator_at(cursors[i].point);
        // backward_through_whitespace(&it);

        uint64_t removed = remove_spaces(&transaction, buffer, it, offset);

        Contents_Iterator eol = it;
        end_of_line(&eol);
        bool at_eol = it.position == eol.position;
        bool insert_backslash = false;
        if (!eol.at_bob()) {
            eol.retreat();
            if (eol.get() == '\\')
                insert_backslash = true;
        }

        if (insert_backslash && !at_eol) {
            Edit bs;
            bs.value = SSOStr::from_constant(" \\");
            bs.position = it.position + offset;
            bs.flags = Edit::INSERT;
            transaction.push(bs);
            offset += bs.value.len();
        }

        uint64_t columns =
            find_indent_width(buffer, it, Discover_Indent_Policy::COPY_PREVIOUS_LINE);
        insert_line_with_indent(&transaction, buffer->mode, it.position, &offset, columns);

        if (insert_backslash && at_eol) {
            Edit bs;
            bs.value = SSOStr::from_constant(" \\");
            bs.position = it.position + offset;
            bs.flags = Edit::INSERT_AFTER_POSITION;
            transaction.push(bs);
            offset += bs.value.len();
        }

        offset -= removed;
    }

    transaction.commit(source.client);
}

struct Invalid_Indent_Data {
    bool invalid;
    uint64_t tabs, spaces;
    uint64_t columns;
};

static Invalid_Indent_Data detect_invalid_indent(const Mode& mode, Contents_Iterator iterator) {
    Invalid_Indent_Data data = {};

    while (!iterator.at_eob()) {
        char ch = iterator.get();
        if (ch == '\n') {
            break;
        }

        if (ch == '\t') {
            ++data.tabs;
            // tab after a space is invalid.
            if (data.spaces > 0) {
                data.invalid = true;
            }
            data.columns += mode.tab_width;
            data.columns -= data.columns % mode.tab_width;
        } else if (ch == ' ') {
            ++data.spaces;
            ++data.columns;
        } else {
            break;
        }

        iterator.advance();
    }

    if (mode.use_tabs && data.spaces >= mode.tab_width) {
        data.invalid = true;
    }

    return data;
}

static void change_line_indent(const Mode& mode,
                               Contents_Iterator iterator,
                               int64_t indent_offset,
                               Transaction* transaction,
                               int64_t* offset,
                               bool allow_alignment) {
    start_of_line(&iterator);

    Invalid_Indent_Data data = detect_invalid_indent(mode, iterator);

    // In general we just add indent_offset but we also need to handle other edge cases.
    uint64_t new_columns = data.columns;
    if (indent_offset > 0) {
        // Increasing indent.
        new_columns += indent_offset;

        // Align with the indent_width.
        if (allow_alignment) {
            new_columns -= new_columns % mode.indent_width;
        }
    } else if ((uint64_t)-indent_offset > new_columns) {
        // Decreasing indent more than the existing so just delete it.
        new_columns = 0;
    } else if (allow_alignment && new_columns % mode.indent_width > 0) {
        // Align with the indent_width.
        new_columns -= new_columns % mode.indent_width;
    } else {
        // Decrease indent (note: indent_offset is negative).
        new_columns += indent_offset;
    }

    uint64_t new_tabs, new_spaces;
    analyze_indent(mode, new_columns, &new_tabs, &new_spaces);

    if (data.invalid) {
        // Remove existing indent.
        Edit remove;
        remove.value = iterator.contents->slice(transaction->value_allocator(), iterator,
                                                iterator.position + data.tabs + data.spaces);
        remove.position = iterator.position + *offset;
        remove.flags = Edit::REMOVE;
        transaction->push(remove);

        // Build buffer with correct number of tabs and spaces.
        char* buffer = (char*)transaction->value_allocator().alloc({new_tabs + new_spaces, 1});
        memset(buffer, '\t', new_tabs);
        memset(buffer + new_tabs, ' ', new_spaces);

        // Then insert correct indent of the same width.
        Edit insert;
        insert.value = SSOStr::from_constant({buffer, new_tabs + new_spaces});
        insert.position = iterator.position + *offset;
        insert.flags = Edit::INSERT;
        transaction->push(insert);

        *offset += insert.value.len() - remove.value.len();
    } else {
        // Go to between tabs and spaces.
        while (!iterator.at_eob()) {
            if (iterator.get() != '\t') {
                break;
            }
            iterator.advance();
        }

        // We may convert tabs -> spaces, spaces -> tabs, or just add / remove tabs and spaces.
        // So we bundle all the add and remove operations together and push one edit for each.
        uint64_t spaces_to_remove = 0, tabs_to_remove = 0;
        uint64_t spaces_to_add = 0, tabs_to_add = 0;
        if (data.spaces > new_spaces) {
            spaces_to_remove = data.spaces - new_spaces;
        } else if (data.spaces < new_spaces) {
            spaces_to_add = new_spaces - data.spaces;
        }
        if (data.tabs > new_tabs) {
            tabs_to_remove = data.tabs - new_tabs;
        } else if (data.tabs < new_tabs) {
            tabs_to_add = new_tabs - data.tabs;
        }

        // Push remove edit.
        if (tabs_to_remove > 0 || spaces_to_remove > 0) {
            char* buffer =
                (char*)transaction->value_allocator().alloc({tabs_to_remove + spaces_to_remove, 1});
            memset(buffer, '\t', tabs_to_remove);
            memset(buffer + tabs_to_remove, ' ', spaces_to_remove);

            Edit remove_indent;
            remove_indent.value =
                SSOStr::from_constant({buffer, tabs_to_remove + spaces_to_remove});
            remove_indent.position = iterator.position + *offset - tabs_to_remove;
            remove_indent.flags = Edit::REMOVE;
            transaction->push(remove_indent);
        }

        // Push add edit.
        if (tabs_to_add > 0 || spaces_to_add > 0) {
            char* buffer =
                (char*)transaction->value_allocator().alloc({tabs_to_add + spaces_to_add, 1});
            memset(buffer, '\t', tabs_to_add);
            memset(buffer + tabs_to_add, ' ', spaces_to_add);

            Edit add_indent;
            add_indent.value = SSOStr::from_constant({buffer, spaces_to_add + tabs_to_add});
            add_indent.position = iterator.position + *offset - tabs_to_remove;
            add_indent.flags = Edit::INSERT;
            transaction->push(add_indent);
        }

        *offset += new_spaces - data.spaces + new_tabs - data.tabs;
    }
}

static void change_indent(Client* client,
                          Window_Unified* window,
                          Buffer* buffer,
                          int64_t indent_offset) {
    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    Contents_Iterator iterator = buffer->contents.start();
    int64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;

    // Only use marks if at least one region is non-empty.
    bool use_marks = false;
    if (window->show_marks) {
        for (size_t i = 0; i < cursors.len; ++i) {
            if (cursors[i].point != cursors[i].mark) {
                use_marks = true;
                break;
            }
        }
    }

    // With a visible region, treat each region independently.
    // Without a visible region, blob all the cursors' lines into one "region".
    //
    // If we have a bunch of lines in the "region", some of them are empty and
    // some are non-empty then we want to only indent at the non-empty lines.
    // But if all the lines are empty then we want to indent at all of them.
    //
    // To account for this in either case we loop through all lines and check for any non-empty
    // lines.  If any non-empty lines are found then we enable filtering which lines are indented.
    if (use_marks) {
        for (size_t i = 0; i < cursors.len; ++i) {
            bool always_indent = true;
            size_t line_count = 0;

            iterator.advance_to(cursors[i].start());
            while (iterator.position < cursors[i].end()) {
                ++line_count;

                // Found a non-empty line so we can skip empty lines.
                if (!at_empty_line(iterator)) {
                    always_indent = false;

                    if (line_count == 1) {
                        end_of_line(&iterator);
                        forward_char(&iterator);
                        if (iterator.position < cursors[i].end()) {
                            ++line_count;
                        }
                    }

                    break;
                }

                end_of_line(&iterator);
                forward_char(&iterator);
            }

            iterator.retreat_to(cursors[i].start());
            while (iterator.position < cursors[i].end()) {
                // Don't indent on empty lines unless all lines are empty.
                if (always_indent || !at_empty_line(iterator)) {
                    change_line_indent(buffer->mode, iterator, indent_offset, &transaction, &offset,
                                       /*allow_alignment=*/line_count == 1);
                }

                end_of_line(&iterator);
                forward_char(&iterator);
            }
        }
    } else {
        // If we have a bunch of cursors, some of them at empty lines and
        // some at empty lines we want to only indent at the non-empty lines.
        // But if all lines are empty then we want to indent at all of them.
        bool always_indent = true;

        for (size_t i = 0; i < cursors.len; ++i) {
            iterator.advance_to(cursors[i].point);

            // Found a non-empty line so we can skip empty lines.
            if (!at_empty_line(iterator)) {
                always_indent = false;
                break;
            }
        }

        iterator.retreat_to(cursors[0].point);
        for (size_t i = 0; i < cursors.len; ++i) {
            iterator.advance_to(cursors[i].point);

            // Don't indent on empty lines unless all lines are empty.
            if (!always_indent && at_empty_line(iterator)) {
                continue;
            }

            change_line_indent(buffer->mode, iterator, indent_offset, &transaction, &offset,
                               /*allow_alignment=*/cursors.len == 1);
        }
    }

    transaction.commit(client);
}

void command_increase_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    change_indent(source.client, window, buffer, buffer->mode.indent_width);
}
void command_decrease_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    change_indent(source.client, window, buffer, -(int64_t)buffer->mode.indent_width);
}

void command_delete_whitespace(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t i = 0; i < cursors.len; ++i) {
        Contents_Iterator it = buffer->contents.iterator_at(cursors[i].point);
        forward_through_whitespace(&it);
        uint64_t end = it.position;
        backward_through_whitespace(&it);
        uint64_t count = end - it.position;
        if (count == 0) {
            continue;
        }

        char* value = (char*)transaction.value_allocator().alloc({count, 1});
        for (Contents_Iterator x = it; x.position < end; x.advance()) {
            value[x.position - it.position] = x.get();
        }

        Edit edit;
        edit.value = SSOStr::from_constant({value, count});
        edit.position = it.position - offset;
        edit.flags = Edit::REMOVE;
        transaction.push(edit);

        offset += count;
    }

    transaction.commit(source.client);
}

void command_merge_lines(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t i = 0; i < cursors.len; ++i) {
        Contents_Iterator start = buffer->contents.iterator_at(cursors[i].point);
        end_of_line(&start);

        if (start.at_eob()) {
            continue;
        }

        Contents_Iterator end = start;
        backward_through_whitespace(&start);
        forward_char(&end);
        forward_through_whitespace(&end);

        Edit remove;
        remove.value = buffer->contents.slice(transaction.value_allocator(), start, end.position);
        remove.position = start.position - offset;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value = SSOStr::from_char(' ');
        insert.position = start.position - offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);

        offset += end.position - start.position;
        --offset;
    }

    transaction.commit(source.client);
}

bool parse_indent_rules(const Contents& contents,
                        uint32_t* indent_width,
                        uint32_t* tab_width,
                        bool* use_tabs) {
    bool found_indent_width = false;
    bool found_tab = false;

    // If we find a line indented with spaces then that is the indent width.
    Contents_Iterator it = contents.start();
    for (int i = 0; i < 10; ++i) {
        if (search_forward_bucket(&it, "\n ")) {
            it.advance();
            uint32_t num_spaces = 0;
            while (looking_at(it, ' ')) {
                ++num_spaces;
                it.advance();
            }

            found_indent_width = true;
            *indent_width = num_spaces;
            break;
        }
    }

    // Determine if tabs are used.
    it = contents.start();
    for (int i = 0; i < 10; ++i) {
        if (search_forward_bucket(&it, "\n\t")) {
            found_tab = true;
            break;
        }
    }

    // If no tabs are present then disable tabs.
    if (found_indent_width && !found_tab) {
        *use_tabs = false;
        *tab_width = *indent_width;
        return true;
    }

    // If there is '\n\t ' then indent_width != tab_width.
    // Assume tab_width = 8, indent_width = num_spaces.
    it = contents.start();
    for (int i = 0; i < 10; ++i) {
        if (search_forward_bucket(&it, "\n\t ")) {
            uint32_t num_spaces = 0;
            while (looking_at(it, ' ')) {
                ++num_spaces;
                it.advance();
            }

            *use_tabs = true;
            *tab_width = 8;
            *indent_width = num_spaces;
            return true;
        }
    }

    // Use tabs.  Set tab_width to default indent_width.
    if (found_tab && !found_indent_width) {
        *use_tabs = true;
        *tab_width = *indent_width;
        return true;
    }

    return false;
}

}
}
