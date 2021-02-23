#include "indent_commands.hpp"

#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace basic {

static uint64_t find_indent_width(const Theme& theme, Buffer* buffer, Contents_Iterator it) {
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

    return depth * theme.indent_width + get_visual_column(theme, end);
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

        alloc_space += find_indent_width(editor->theme, buffer, it);
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

        uint64_t columns = find_indent_width(editor->theme, buffer, it);

        uint64_t num_tabs, num_spaces;
        analyze_indent(editor->theme, columns, &num_tabs, &num_spaces);

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

/*
void command_auto_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    uint64_t count = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        count += find_indent_width(buffer->contents.iterator_at(cursors[i].point));
    }

    Transaction transaction;
    transaction.init(cursors.len, count);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        uint64_t count = find_indent_width(buffer->contents.iterator_at(cursors[i].point));

        char* value = (char*)transaction.value_allocator().alloc({count, 1});
        memset(value, ' ', count);

        Edit edit;
        edit.value.init_from_constant({value, count});
        edit.position = cursors[i].point + offset;
        edit.flags = Edit::INSERT;
        transaction.push(edit);

        offset += count;
    }

    transaction.commit(buffer);
}
*/

static bool at_empty_line(Contents_Iterator iterator) {
    return (iterator.at_eob() || iterator.get() == '\n') &&
           (iterator.at_bob() || (iterator.retreat(), iterator.get() == '\n'));
}

static void change_indent(Editor* editor, Command_Source source, int64_t indent_offset) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    uint64_t edits = 0;
    uint64_t alloc_len = 0;

    Contents_Iterator iterator = buffer->contents.start();
    for (size_t i = 0; i < cursors.len; ++i) {
        iterator.advance_to(cursors[i].point);
        if (cursors.len > 1 && at_empty_line(iterator)) {
            --edits;
            continue;
        }

        forward_through_whitespace(&iterator);
        uint64_t end = iterator.position;
        backward_through_whitespace(&iterator);

        uint64_t old_columns = count_visual_columns(editor->theme, iterator, end);
        uint64_t old_tabs, old_spaces, new_tabs, new_spaces;
        analyze_indent(editor->theme, old_columns, &old_tabs, &old_spaces);
        analyze_indent(editor->theme, old_columns + indent_offset, &new_tabs, &new_spaces);

        bool adding = false, removing = false;
        if (old_spaces > new_spaces) {
            removing = true;
            alloc_len += old_spaces - new_spaces;
        } else if (old_spaces < new_spaces) {
            adding = true;
            alloc_len += new_spaces - old_spaces;
        }
        if (old_tabs > new_tabs) {
            removing = true;
            alloc_len += old_tabs - new_tabs;
        } else if (old_tabs < new_tabs) {
            adding = true;
            alloc_len += new_tabs - old_tabs;
        }
        edits += adding + removing;
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

        Contents_Iterator end_of_whitespace = iterator;
        forward_through_whitespace(&end_of_whitespace);
        uint64_t end = end_of_whitespace.position;

        // Go to between tabs and spaces.
        backward_through_whitespace(&iterator);
        Contents_Iterator start_of_whitespace = iterator;
        while (!iterator.at_eob()) {
            if (iterator.get() != '\t') {
                break;
            }
            iterator.advance();
        }

        uint64_t old_columns = count_visual_columns(editor->theme, start_of_whitespace, end);
        uint64_t old_tabs, old_spaces, new_tabs, new_spaces;
        analyze_indent(editor->theme, old_columns, &old_tabs, &old_spaces);
        analyze_indent(editor->theme, old_columns + indent_offset, &new_tabs, &new_spaces);

        bool adding = false, removing = false;
        uint64_t spaces_to_remove = 0, tabs_to_remove = 0;
        uint64_t spaces_to_add = 0, tabs_to_add = 0;
        if (old_spaces > new_spaces) {
            removing = true;
            spaces_to_remove = old_spaces - new_spaces;
        } else if (old_spaces < new_spaces) {
            adding = true;
            spaces_to_add = new_spaces - old_spaces;
        }
        if (old_tabs > new_tabs) {
            removing = true;
            tabs_to_remove = old_tabs - new_tabs;
        } else if (old_tabs < new_tabs) {
            adding = true;
            tabs_to_add = new_tabs - old_tabs;
        }

        if (removing) {
            char* buffer =
                (char*)transaction.value_allocator().alloc({tabs_to_remove + spaces_to_remove, 1});
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

        offset += (int64_t)0 + new_spaces - old_spaces + new_tabs - old_tabs;
    }

    transaction.commit(buffer);
}

void command_increase_indent(Editor* editor, Command_Source source) {
    return change_indent(editor, source, editor->theme.indent_width);
}
void command_decrease_indent(Editor* editor, Command_Source source) {
    return change_indent(editor, source, -(int64_t)editor->theme.indent_width);
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
