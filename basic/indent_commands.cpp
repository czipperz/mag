#include "indent_commands.hpp"

#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace basic {

static uint64_t find_indent_width(Buffer* buffer, Contents_Iterator it) {
    Contents_Iterator original = it;

    // Find a line backwards that isn't empty (including the current line).
    while (!it.at_bob()) {
        start_of_line(&it);

        if (!it.at_eob() && it.get() != '\n') {
            break;
        }

        it.retreat();
    }

    // Look at all tokens between it and original and add one indentation (4) for each unmatched
    // open pair.
    int64_t depth = 0;

    if (buffer->mode.indent_after_open_pair) {
        Tokenizer_Check_Point check_point = {};
        buffer->token_cache.update(buffer);
        buffer->token_cache.find_check_point(it.position, &check_point);

        Contents_Iterator token_iterator = it;
        token_iterator.retreat_to(check_point.position);
        uint64_t state = check_point.state;

        Token token;
        while (buffer->mode.next_token(&token_iterator, &token, &state)) {
            if (token.start < it.position) {
                continue;
            }
            if (token.end > original.position) {
                break;
            }
            if (token.type == Token_Type::OPEN_PAIR) {
                ++depth;
            }
            if (token.type == Token_Type::CLOSE_PAIR) {
                --depth;
            }
        }

        // Don't unindent if the line has more close pairs.  A line `}`
        // shouldn't cause the next line to be unindented even more.
        // It should be indented the same amount as the closing pair.
        depth = cz::max(depth, (int64_t)0);
    }

    Contents_Iterator end = it;
    forward_through_whitespace(&end);

    return depth * buffer->mode.indent_width + get_visual_column(buffer->mode, end);
}

void command_insert_newline_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    int64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t i = 0; i < cursors.len; ++i) {
        Contents_Iterator it = buffer->contents.iterator_at(cursors[i].point);
        forward_through_whitespace(&it);
        uint64_t end = it.position;
        backward_through_whitespace(&it);

        Edit remove;
        remove.value = buffer->contents.slice(transaction.value_allocator(), it, end);
        remove.position = it.position + offset;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        uint64_t columns = find_indent_width(buffer, it);

        uint64_t num_tabs, num_spaces;
        analyze_indent(buffer->mode, columns, &num_tabs, &num_spaces);

        char* value = (char*)transaction.value_allocator().alloc({1 + num_tabs + num_spaces, 1});
        value[0] = '\n';
        memset(value + 1, '\t', num_tabs);
        memset(value + 1 + num_tabs, ' ', num_spaces);

        Edit insert;
        insert.value = SSOStr::from_constant({value, 1 + num_tabs + num_spaces});
        insert.position = it.position + offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);

        offset += insert.value.len() - remove.value.len();
    }

    transaction.commit();
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

static void change_indent(Window_Unified* window, Buffer* buffer, int64_t indent_offset) {
    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    Contents_Iterator iterator = buffer->contents.start();
    int64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;

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

        start_of_line(&iterator);

        Invalid_Indent_Data data = detect_invalid_indent(buffer->mode, iterator);

        // In general we just add indent_offset but we also need to handle other edge cases.
        uint64_t new_columns = data.columns;
        if (indent_offset > 0) {
            // Increasing indent.
            new_columns += indent_offset;

            // Align with the indent_width.
            if (cursors.len == 1) {
                new_columns -= new_columns % buffer->mode.indent_width;
            }
        } else if ((uint64_t)-indent_offset > new_columns) {
            // Decreasing indent more than the existing so just delete it.
            new_columns = 0;
        } else if (cursors.len == 1 && new_columns % buffer->mode.indent_width > 0) {
            // Align with the indent_width.
            new_columns -= new_columns % buffer->mode.indent_width;
        } else {
            // Decrease indent (note: indent_offset is negative).
            new_columns += indent_offset;
        }

        uint64_t new_tabs, new_spaces;
        analyze_indent(buffer->mode, new_columns, &new_tabs, &new_spaces);

        if (data.invalid) {
            // Remove existing indent.
            Edit remove;
            remove.value = buffer->contents.slice(transaction.value_allocator(), iterator,
                                                  iterator.position + data.tabs + data.spaces);
            remove.position = iterator.position + offset;
            remove.flags = Edit::REMOVE;
            transaction.push(remove);

            // Build buffer with correct number of tabs and spaces.
            char* buffer = (char*)transaction.value_allocator().alloc({new_tabs + new_spaces, 1});
            memset(buffer, '\t', new_tabs);
            memset(buffer + new_tabs, ' ', new_spaces);

            // Then insert correct indent of the same width.
            Edit insert;
            insert.value = SSOStr::from_constant({buffer, new_tabs + new_spaces});
            insert.position = iterator.position + offset;
            insert.flags = Edit::INSERT;
            transaction.push(insert);

            offset += insert.value.len() - remove.value.len();
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
                char* buffer = (char*)transaction.value_allocator().alloc(
                    {tabs_to_remove + spaces_to_remove, 1});
                memset(buffer, '\t', tabs_to_remove);
                memset(buffer + tabs_to_remove, ' ', spaces_to_remove);

                Edit remove_indent;
                remove_indent.value =
                    SSOStr::from_constant({buffer, tabs_to_remove + spaces_to_remove});
                remove_indent.position = iterator.position + offset - tabs_to_remove;
                remove_indent.flags = Edit::REMOVE;
                transaction.push(remove_indent);
            }

            // Push add edit.
            if (tabs_to_add > 0 || spaces_to_add > 0) {
                char* buffer =
                    (char*)transaction.value_allocator().alloc({tabs_to_add + spaces_to_add, 1});
                memset(buffer, '\t', tabs_to_add);
                memset(buffer + tabs_to_add, ' ', spaces_to_add);

                Edit add_indent;
                add_indent.value = SSOStr::from_constant({buffer, spaces_to_add + tabs_to_add});
                add_indent.position = iterator.position + offset - tabs_to_remove;
                add_indent.flags = Edit::INSERT;
                transaction.push(add_indent);
            }

            offset += (int64_t)0 + new_spaces - data.spaces + new_tabs - data.tabs;
        }
    }

    transaction.commit();
}

void command_increase_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    change_indent(window, buffer, buffer->mode.indent_width);
}
void command_decrease_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    change_indent(window, buffer, -(int64_t)buffer->mode.indent_width);
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

    transaction.commit();
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

    transaction.commit();
}

}
}
