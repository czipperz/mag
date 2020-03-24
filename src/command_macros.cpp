#include "command_macros.hpp"

#include "editor.hpp"

namespace mag {

void insert(Buffer* buffer, Window_Unified* window, SSOStr value) {
    window->update_cursors(buffer->changes);

    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(cursors.len, 0);
    CZ_DEFER(transaction.drop());

    for (size_t i = 0; i < cursors.len; ++i) {
        Edit edit;
        edit.value = value;
        edit.position = cursors[i].point + i;
        edit.is_insert = true;
        transaction.push(edit);
    }

    transaction.commit(buffer);
}

void insert_char(Buffer* buffer, Window_Unified* window, char code) {
    SSOStr value;
    value.init_char(code);
    insert(buffer, window, value);
}

}
