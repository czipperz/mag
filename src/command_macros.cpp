#include "command_macros.hpp"

#include "editor.hpp"

namespace mag {

void insert(Editor* editor, Client* client, SSOStr value) {
    WITH_BUFFER(buffer, client->selected_buffer_id(), WITH_TRANSACTION({
                    transaction.reserve(buffer->cursors.len());
                    for (size_t i = 0; i < buffer->cursors.len(); ++i) {
                        Edit edit;
                        edit.value = value;
                        edit.position = buffer->cursors[i].point + i;
                        edit.is_insert = true;
                        transaction.push(edit);
                    }
                }));
}

void insert_char(Editor* editor, Client* client, char code) {
    SSOStr value;
    value.init_char(code);
    insert(editor, client, value);
}

}
