#include "command_macros.hpp"

#include "editor.hpp"

namespace mag {

void insert(Buffer* buffer, Window_Unified* window, SSOStr value) {
    WITH_TRANSACTION({
        window->update_cursors(buffer->changes);

        cz::Slice<Cursor> cursors = window->cursors;
        transaction.init(cursors.len, 0);
        for (size_t i = 0; i < cursors.len; ++i) {
            Edit edit;
            edit.value = value;
            edit.position = cursors[i].point + i;
            edit.is_insert = true;
            transaction.push(edit);
        }
    });
}

void insert_char(Buffer* buffer, Window_Unified* window, char code) {
    SSOStr value;
    value.init_char(code);
    insert(buffer, window, value);
}

}
