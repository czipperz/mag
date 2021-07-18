#include "indent_commands.hpp"

#include "basic/token_movement_commands.hpp"
#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "insert.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace basic {

void command_insert_open_pair(Editor* editor, Command_Source source) {
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
                return do_command_insert_char(buffer, window, source);
            }
        }
    }

    Key key = source.keys[0];
    CZ_ASSERT(key.modifiers == 0);

    if (key.code == '{') {
        insert(buffer, window, SSOStr::from_constant("{}"));
    } else if (key.code == '(') {
        insert(buffer, window, SSOStr::from_constant("()"));
    } else if (key.code == '[') {
        insert(buffer, window, SSOStr::from_constant("[]"));
    } else {
        CZ_PANIC("command_insert_open_pair: no corresponding pair");
    }

    window->update_cursors(buffer);

    // Go to after the open pair.
    for (size_t i = 0; i < cursors.len; ++i) {
        cursors[i].point--;
    }
}

void command_insert_close_pair(Editor* editor, Command_Source source) {
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
                return do_command_insert_char(buffer, window, source);
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

    transaction.commit();
}

}
}
