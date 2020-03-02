#include "server.hpp"

#include <cz/heap.hpp>
#include "client.hpp"
#include "command_macros.hpp"

namespace mag {

Client Server::make_client() {
    Client client = {};
    Buffer_Id selected_buffer_id = {editor.buffers.len() - 1};
    editor.create_buffer("*client mini buffer*");
    Buffer_Id mini_buffer_id = {editor.buffers.len() - 1};
    client.init(selected_buffer_id, mini_buffer_id);
    return client;
}

static void insert_typed(Editor* editor, Client* client, char code) {
    WITH_BUFFER(buffer, client->selected_buffer_id(), WITH_TRANSACTION({
                    transaction.reserve(buffer->cursors.len());

                    SSOStr value;
                    value.init_char(code);
                    for (size_t i = 0; i < buffer->cursors.len(); ++i) {
                        Edit edit;
                        edit.value = value;
                        edit.position = buffer->cursors[i].point + i;
                        edit.is_insert = true;
                        transaction.push(edit);
                    }
                }));
}

static void send_message_result(Editor* editor, Client* client) {
    // todo don't lock mini buffer so other people can use it
    WITH_BUFFER(mini_buffer, client->mini_buffer_id(), {
        client->restore_selected_buffer();
        client->_message.response_callback(editor, client, mini_buffer,
                                           client->_message.response_callback_data);
        client->dealloc_message();
    });
}

void Server::receive(Client* client, Key key) {
    client->key_chain.reserve(cz::heap_allocator(), 1);
    client->key_chain.push(key);

    size_t i = 0;
top:
    size_t start = i;
    Key_Map* map = &editor.key_map;
    for (; i < client->key_chain.len(); ++i) {
        Key_Bind* bind = map->lookup(client->key_chain[i]);
        if (bind == nullptr) {
            if (start == i && key.modifiers == 0 &&
                (isprint(key.code) || key.code == '\t' || key.code == '\n')) {
                ++i;

                if (key.code == '\n' && client->selected_buffer_id() == client->mini_buffer_id()) {
                    send_message_result(&editor, client);
                } else {
                    insert_typed(&editor, client, key.code);
                }
            } else {
                ++i;

                Message message = {};
                message.tag = Message::SHOW;
                message.text = "Invalid key combo";
                client->show_message(message);
            }
            goto top;
        }

        if (bind->is_command) {
            Command_Source source;
            source.client = client;
            ++i;
            source.keys = {client->key_chain.start() + start, i - start};
            bind->v.command(&editor, source);
            goto top;
        } else {
            map = bind->v.map;
        }
    }

    client->key_chain.remove_range(0, start);
}

}
