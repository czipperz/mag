#include "ide_commands.hpp"

#include "basic/indent_commands.hpp"
#include "basic/token_movement_commands.hpp"
#include "core/command.hpp"
#include "core/command_macros.hpp"
#include "core/editor.hpp"
#include "core/insert.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"

namespace mag {
namespace basic {

REGISTER_COMMAND(command_insert_open_pair);
void command_insert_open_pair(Editor* editor, Command_Source source) {
    if (source.keys.len == 0) {
        source.client->show_message("command_insert_open_pair must be called via keybind");
        return;
    }

    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    // If any cursors are in a token that is raw text then just insert the open pair.
    Contents_Iterator it = buffer->contents.start();
    for (size_t i = 0; i < cursors.len; ++i) {
        it.advance_to(cursors[i].point);

        // If at eob or at a space then we want to insert a pair.  Otherwise just one.
        if (!it.at_eob() && !cz::is_space(it.get())) {
            return do_command_insert_char(editor, buffer, window, source);
        }

        Contents_Iterator token_iterator = it;
        Token token;
        if (get_token_at_position(buffer, &token_iterator, &token)) {
            if (token.type == Token_Type::STRING || token.type == Token_Type::COMMENT ||
                token.type == Token_Type::DOC_COMMENT) {
                return do_command_insert_char(editor, buffer, window, source);
            }
        }
    }

    Key key = source.keys[0];
    CZ_ASSERT(key.modifiers == 0);

    if (key.code == '{') {
        insert(source.client, buffer, window, SSOStr::from_constant("{}"));
    } else if (key.code == '(') {
        insert(source.client, buffer, window, SSOStr::from_constant("()"));
    } else if (key.code == '[') {
        insert(source.client, buffer, window, SSOStr::from_constant("[]"));
    } else {
        CZ_PANIC("command_insert_open_pair: no corresponding pair");
    }

    window->update_cursors(buffer, source.client);

    // Go to after the open pair.
    for (size_t i = 0; i < cursors.len; ++i) {
        cursors[i].point--;
    }
}

REGISTER_COMMAND(command_insert_close_pair);
void command_insert_close_pair(Editor* editor, Command_Source source) {
    if (source.keys.len == 0) {
        source.client->show_message("command_insert_close_pair must be called via keybind");
        return;
    }

    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    // If any cursors are in a token that is raw text then just insert the open pair.
    Contents_Iterator it = buffer->contents.start();
    for (size_t i = 0; i < cursors.len; ++i) {
        it.advance_to(cursors[i].point);
        Contents_Iterator token_iterator = it;
        Token token;
        if (get_token_at_position(buffer, &token_iterator, &token)) {
            if (token.type == Token_Type::STRING || token.type == Token_Type::COMMENT ||
                token.type == Token_Type::DOC_COMMENT) {
                return do_command_insert_char(editor, buffer, window, source);
            }
        }
    }

    Key key = source.keys[0];
    CZ_ASSERT(key.modifiers == 0);
    CZ_ASSERT(key.code == (char)key.code);
    CZ_ASSERT(cz::is_print((char)key.code));

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    it.retreat_to(cursors[0].point);
    for (size_t i = 0; i < cursors.len; ++i) {
        it.advance_to(cursors[i].point);

        if (it.at_eob() || it.get() != key.code) {
            Edit edit;
            edit.value = SSOStr::from_char((char)key.code);
            edit.position = cursors[i].point + offset;
            edit.flags = Edit::INSERT;
            transaction.push(edit);

            offset++;
        } else {
            cursors[i].point++;
        }
    }

    transaction.commit(source.client);
}

REGISTER_COMMAND(command_insert_newline_split_pairs);
void command_insert_newline_split_pairs(Editor* editor, Command_Source source) {
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

        if (!it.at_bob() && !it.at_eob()) {
            Contents_Iterator before_it = it;
            before_it.retreat();
            char before = before_it.get();
            char after = it.get();
            if (before == '{' && after == '}') {
                uint64_t columns =
                    find_indent_width(buffer, it, Discover_Indent_Policy::COPY_PREVIOUS_LINE);
                columns += buffer->mode.indent_width;

                // Insert the line for the cursor.
                insert_line_with_indent(&transaction, buffer->mode, it.position, &offset, columns);

                // Insert the line for the close pair.
                insert_line_with_indent(&transaction, buffer->mode, it.position, &offset,
                                        columns - buffer->mode.indent_width);
                transaction.edits.last().flags = Edit::INSERT_AFTER_POSITION;
                continue;
            }
        }

        uint64_t columns = find_indent_width(buffer, it);
        insert_line_with_indent(&transaction, buffer->mode, it.position, &offset, columns);

        offset -= removed;
    }

    transaction.commit(source.client);
}

REGISTER_COMMAND(command_insert_pair);
void command_insert_pair(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    Key key = source.keys[0];
    CZ_ASSERT(key.code == (char)key.code);
    CZ_ASSERT(cz::is_print((char)key.code));

    char open = (char)key.code;
    char close;
    if (open == '(') {
        close = ')';
    } else if (open == '[') {
        close = ']';
    } else if (open == '{') {
        close = '}';
    } else {
        CZ_PANIC("command_insert_pair: no corresponding pair");
    }

    uint64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t i = 0; i < cursors.len; ++i) {
        Edit edit1;
        edit1.value = SSOStr::from_char(open);
        edit1.position = cursors[i].point + offset;
        edit1.flags = Edit::INSERT;
        transaction.push(edit1);
        ++offset;

        Edit edit2;
        edit2.value = SSOStr::from_char(close);
        edit2.position = cursors[i].point + offset;
        edit2.flags = Edit::INSERT_AFTER_POSITION;
        transaction.push(edit2);
        ++offset;
    }

    transaction.commit(source.client);
}

}
}
