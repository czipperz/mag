#include "indent_commands.hpp"

#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "movement.hpp"

namespace mag {
namespace basic {

static uint64_t find_indent_width(Contents_Iterator it) {
    Contents_Iterator end;
    while (1) {
        end = it;
        backward_through_whitespace(&it);
        if (it.at_bob()) {
            break;
        }

        Contents_Iterator x = it;
        x.retreat();
        if (x.get() == '\n') {
            break;
        }

        it = x;
    }

    return end.position - it.position;
}

void command_insert_newline_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    uint64_t count = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        Contents_Iterator it = buffer->contents.iterator_at(cursors[i].point);
        forward_through_whitespace(&it);
        uint64_t spaces = it.position;
        backward_through_whitespace(&it);
        spaces -= it.position;

        uint64_t offset = find_indent_width(it);
        if (offset > spaces) {
            count += offset - spaces;
        }
    }

    Transaction transaction;
    transaction.init(cursors.len, count + cursors.len);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        Contents_Iterator it = buffer->contents.iterator_at(cursors[i].point);
        forward_through_whitespace(&it);
        uint64_t spaces = it.position;
        backward_through_whitespace(&it);
        spaces -= it.position;

        uint64_t count = find_indent_width(it);
        if (count > spaces) {
            count -= spaces;
        } else {
            count = 0;
        }

        char* value = (char*)transaction.value_allocator().alloc({count + 1, 1});
        value[0] = '\n';
        memset(value + 1, ' ', count);

        Edit edit;
        edit.value.init_from_constant({value, count + 1});
        edit.position = it.position + offset;
        edit.flags = Edit::INSERT;
        transaction.push(edit);

        offset += count + 1;
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

    SSOStr value;
    value.init_from_constant("    ");

    Transaction transaction;
    transaction.init(cursors.len, 0);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        Edit edit;
        edit.value = value;
        edit.position = cursors[i].point + offset;
        edit.flags = Edit::INSERT;
        transaction.push(edit);

        offset += value.len();
    }

    transaction.commit(buffer);
}

void command_delete_whitespace(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    uint64_t count = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        Contents_Iterator it = buffer->contents.iterator_at(cursors[i].point);
        forward_through_whitespace(&it);
        uint64_t end = it.position;
        backward_through_whitespace(&it);
        count += end - it.position;
    }

    Transaction transaction;
    transaction.init(cursors.len, count);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        Contents_Iterator it = buffer->contents.iterator_at(cursors[i].point);
        forward_through_whitespace(&it);
        uint64_t end = it.position;
        backward_through_whitespace(&it);
        uint64_t count = end - it.position;

        char* value = (char*)transaction.value_allocator().alloc({count, 1});
        for (Contents_Iterator x = it; x.position < end; x.advance()) {
            value[x.position - it.position] = x.get();
        }

        Edit edit;
        edit.value.init_from_constant({value, count});
        edit.position = it.position + offset;
        edit.flags = Edit::REMOVE;
        transaction.push(edit);

        offset += count;
    }

    transaction.commit(buffer);
}

}
}
