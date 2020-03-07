#include "server.hpp"

#include <cz/heap.hpp>
#include "client.hpp"
#include "command_macros.hpp"

namespace mag {

Client Server::make_client() {
    Client client = {};
    Buffer_Id selected_buffer_id = {editor.buffers.len() - 1};
    Buffer_Id mini_buffer_id = editor.create_buffer("*client mini buffer*");
    client.init(selected_buffer_id, mini_buffer_id);
    return client;
}

void clear_buffer(Editor* editor, Buffer* buffer) {
    WITH_TRANSACTION({
        uint64_t contents_len = buffer->contents.len;
        transaction.init(1, (size_t)contents_len);
        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(), 0, contents_len);
        edit.position = 0;
        edit.is_insert = false;
        transaction.push(edit);
    });
}

static void send_message_result(Editor* editor, Client* client) {
    // todo don't lock mini buffer so other people can use it
    WITH_BUFFER(mini_buffer, client->mini_buffer_id(), {
        client->restore_selected_buffer();
        client->_message.response_callback(editor, client, mini_buffer,
                                           client->_message.response_callback_data);
        client->dealloc_message();

        clear_buffer(editor, mini_buffer);
    });
}

static void command_insert_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        char code = source.keys[0].code;

        if (source.previous_command == command_insert_char) {
            CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len());
            Commit commit = buffer->commits[buffer->commit_index - 1];
            size_t len = commit.edits[0].value.len();
            if (len < SSOStr::MAX_SHORT_LEN) {
                CZ_DEBUG_ASSERT(commit.edits.len == buffer->cursors.len());
                buffer->undo();

                WITH_TRANSACTION({
                    transaction.init(commit.edits.len, 0);
                    for (size_t e = 0; e < commit.edits.len; ++e) {
                        CZ_DEBUG_ASSERT(commit.edits[e].value.is_short());
                        CZ_DEBUG_ASSERT(commit.edits[e].value.len() == len);

                        Edit edit;
                        memcpy(edit.value.short_._buffer, commit.edits[e].value.short_._buffer,
                               len);
                        edit.value.short_._buffer[len] = code;
                        edit.value.short_.set_len(len + 1);
                        edit.position = commit.edits[e].position + e;
                        edit.is_insert = true;
                        transaction.push(edit);
                    }
                });
                return;
            }
        }

        insert_char(buffer, code);
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
                    previous_command = nullptr;
                } else {
                    Command_Source source;
                    source.client = client;
                    source.keys = {client->key_chain.start() + start, i - start};
                    source.previous_command = previous_command;

                    command_insert_char(&editor, source);
                    previous_command = command_insert_char;
                }
            } else {
                ++i;

                Message message = {};
                message.tag = Message::SHOW;
                message.text = "Invalid key combo";
                client->show_message(message);
                previous_command = nullptr;
            }
            goto top;
        }

        if (bind->is_command) {
            ++i;

            Command_Source source;
            source.client = client;
            source.keys = {client->key_chain.start() + start, i - start};
            source.previous_command = previous_command;

            Command command = bind->v.command;
            command(&editor, source);
            previous_command = command;
            goto top;
        } else {
            map = bind->v.map;
        }
    }

    client->key_chain.remove_range(0, start);
}

}
