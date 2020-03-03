#include "command_macros.hpp"

#include "editor.hpp"

namespace mag {

void insert(Buffer* buffer, SSOStr value) {
    WITH_TRANSACTION({
        transaction.init(buffer->cursors.len(), 0);
        for (size_t i = 0; i < buffer->cursors.len(); ++i) {
            Edit edit;
            edit.value = value;
            edit.position = buffer->cursors[i].point + i;
            edit.is_insert = true;
            transaction.push(edit);
        }
    });
}

void insert_char(Buffer* buffer, char code) {
    SSOStr value;
    value.init_char(code);
    insert(buffer, value);
}

}
