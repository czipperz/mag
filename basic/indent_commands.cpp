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

void command_insert_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    SSOStr value = SSOStr::from_constant("    ");

    Transaction transaction;
    transaction.init(cursors.len, 0);
    CZ_DEFER(transaction.drop());

    Contents_Iterator iterator = buffer->contents.start();

    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        iterator.advance_to(cursors[i].point);

        Edit edit;
        if (!iterator.at_eob() && iterator.get() == '\t') {
            edit.value = SSOStr::from_char('\t');
        } else if (cursors.len > 1 && (iterator.at_eob() || iterator.get() == '\n') &&
                   (iterator.at_bob() || (iterator.retreat(), iterator.get() == '\n'))) {
            continue;
        } else {
            edit.value = value;
        }

        edit.position = cursors[i].point + offset;
        edit.flags = Edit::INSERT;
        transaction.push(edit);

        offset += edit.value.short_.len();
    }

    transaction.commit(buffer);
}

void command_delete_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    SSOStr value = SSOStr::from_constant("    ");

    Transaction transaction;
    transaction.init(cursors.len, 0);
    CZ_DEFER(transaction.drop());

    Contents_Iterator iterator = buffer->contents.start();

    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        iterator.advance_to(cursors[i].point);

        Edit edit;
        if (!iterator.at_eob() && iterator.get() == '\t') {
            edit.value = SSOStr::from_char('\t');
        } else {
            uint64_t num;
            for (num = 0; num < 4; ++num) {
                if (iterator.at_bob()) {
                    break;
                }
                iterator.retreat();
                if (iterator.get() != ' ') {
                    iterator.advance();
                    break;
                }
            }

            if (num != 4) {
                for (num = 0; num < 4; ++num) {
                    if (iterator.at_eob() || iterator.get() != ' ') {
                        break;
                    }
                    iterator.advance();
                }
                iterator.retreat(num);
            }

            if (num == 0) {
                continue;
            } else {
                edit.value = value;
                edit.value.short_.set_len(num);
            }
        }

        edit.position = iterator.position - offset;
        edit.flags = Edit::REMOVE;
        transaction.push(edit);

        offset += edit.value.short_.len();
    }

    transaction.commit(buffer);
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
