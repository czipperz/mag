#include "indent_commands.hpp"

#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace basic {

static uint64_t find_indent_width(const Mode& mode, Buffer* buffer, Contents_Iterator it) {
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
    // Don't unindent if the line has more close pairs.  A line `}` shouldn't cause the next line to
    // be unindented even more.  It should be indented the same amount as the closing pair.
    depth = cz::max(depth, (int64_t)0);

    Contents_Iterator end = it;
    forward_through_whitespace(&end);

    return depth * mode.indent_width + get_visual_column(mode, end);
}

void command_insert_newline_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    uint64_t alloc_space = cursors.len;  // 1 newline per cursor
    for (size_t i = 0; i < cursors.len; ++i) {
        Contents_Iterator it = buffer->contents.iterator_at(cursors[i].point);
        forward_through_whitespace(&it);
        uint64_t end = it.position;
        backward_through_whitespace(&it);
        alloc_space += end - it.position;

        alloc_space += find_indent_width(buffer->mode, buffer, it);
    }

    Transaction transaction;
    transaction.init(cursors.len * 2, alloc_space);
    CZ_DEFER(transaction.drop());

    int64_t offset = 0;
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

        uint64_t columns = find_indent_width(buffer->mode, buffer, it);

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

    transaction.commit(buffer);
}

static bool at_empty_line(Contents_Iterator iterator) {
    return (iterator.at_eob() || iterator.get() == '\n') &&
           (iterator.at_bob() || (iterator.retreat(), iterator.get() == '\n'));
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
    cz::Slice<Cursor> cursors = window->cursors;

    uint64_t edits = 0;
    uint64_t alloc_len = 0;

    Contents_Iterator iterator = buffer->contents.start();
    for (size_t i = 0; i < cursors.len; ++i) {
        iterator.advance_to(cursors[i].point);
        if (cursors.len > 1 && at_empty_line(iterator)) {
            continue;
        }

        start_of_line(&iterator);

        Invalid_Indent_Data data = detect_invalid_indent(buffer->mode, iterator);

        uint64_t new_columns = data.columns;
        if (indent_offset < 0 && -indent_offset > new_columns) {
            new_columns = 0;
        } else {
            new_columns += indent_offset;
            new_columns -= new_columns % buffer->mode.indent_width;
        }
        uint64_t new_tabs, new_spaces;
        analyze_indent(buffer->mode, new_columns, &new_tabs, &new_spaces);

        if (data.invalid) {
            edits += 2;
            alloc_len += data.tabs + data.spaces;
            alloc_len += new_tabs + new_spaces;
        } else {
            bool adding = false, removing = false;
            if (data.spaces > new_spaces) {
                removing = true;
                alloc_len += data.spaces - new_spaces;
            } else if (data.spaces < new_spaces) {
                adding = true;
                alloc_len += new_spaces - data.spaces;
            }
            if (data.tabs > new_tabs) {
                removing = true;
                alloc_len += data.tabs - new_tabs;
            } else if (data.tabs < new_tabs) {
                adding = true;
                alloc_len += new_tabs - data.tabs;
            }
            edits += adding + removing;
        }
    }

    Transaction transaction;
    transaction.init(edits, alloc_len);
    CZ_DEFER(transaction.drop());

    iterator = buffer->contents.start();
    int64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        iterator.advance_to(cursors[i].point);
        if (cursors.len > 1 && at_empty_line(iterator)) {
            continue;
        }

        start_of_line(&iterator);

        Invalid_Indent_Data data = detect_invalid_indent(buffer->mode, iterator);

        uint64_t new_columns = data.columns;
        if (indent_offset < 0 && -indent_offset > new_columns) {
            new_columns = 0;
        } else {
            new_columns += indent_offset;
            new_columns -= new_columns % buffer->mode.indent_width;
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

            bool adding = false, removing = false;
            uint64_t spaces_to_remove = 0, tabs_to_remove = 0;
            uint64_t spaces_to_add = 0, tabs_to_add = 0;
            if (data.spaces > new_spaces) {
                removing = true;
                spaces_to_remove = data.spaces - new_spaces;
            } else if (data.spaces < new_spaces) {
                adding = true;
                spaces_to_add = new_spaces - data.spaces;
            }
            if (data.tabs > new_tabs) {
                removing = true;
                tabs_to_remove = data.tabs - new_tabs;
            } else if (data.tabs < new_tabs) {
                adding = true;
                tabs_to_add = new_tabs - data.tabs;
            }

            if (removing) {
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

            if (adding) {
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

    transaction.commit(buffer);
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

    cz::Slice<Cursor> cursors = window->cursors;

    uint64_t count = 0;
    size_t edits = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        Contents_Iterator it = buffer->contents.iterator_at(cursors[i].point);
        forward_through_whitespace(&it);
        uint64_t end = it.position;
        backward_through_whitespace(&it);
        uint64_t local_count = end - it.position;
        if (local_count > 0) {
            ++edits;
        }
        count += local_count;
    }

    Transaction transaction;
    transaction.init(edits, count);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
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

    transaction.commit(buffer);
}

}
}
