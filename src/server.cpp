#include "server.hpp"

#include <Tracy.hpp>
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

cz::Str clear_buffer(Editor* editor, Buffer* buffer) {
    Transaction transaction;
    transaction.init(1, (size_t)buffer->contents.len);
    CZ_DEFER(transaction.drop());

    Edit edit;
    edit.value = buffer->contents.slice(transaction.value_allocator(),
                                        buffer->contents.iterator_at(0), buffer->contents.len);
    edit.position = 0;
    edit.flags = Edit::REMOVE;
    transaction.push(edit);

    cz::Str buffer_contents = transaction.last_edit_value();

    transaction.commit(buffer);

    return buffer_contents;
}

static void send_message_result(Editor* editor, Client* client) {
    cz::Str mini_buffer_contents;
    {
        Window_Unified* window = client->mini_buffer_window();
        WITH_WINDOW_BUFFER(window);
        mini_buffer_contents = clear_buffer(editor, buffer);
    }

    client->restore_selected_buffer();
    client->_message.response_callback(editor, client, mini_buffer_contents,
                                       client->_message.response_callback_data);
    client->dealloc_message();
}

static void command_insert_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    char code = source.keys[0].code;

    if (source.previous_command == command_insert_char) {
        CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len());
        Commit commit = buffer->commits[buffer->commit_index - 1];
        size_t len = commit.edits[0].value.len();
        if (len < SSOStr::MAX_SHORT_LEN) {
            CZ_DEBUG_ASSERT(commit.edits.len == window->cursors.len());
            buffer->undo();
            // We don't need to update cursors here because insertion doesn't care.

            Transaction transaction;
            transaction.init(commit.edits.len, 0);
            CZ_DEFER(transaction.drop());

            for (size_t e = 0; e < commit.edits.len; ++e) {
                CZ_DEBUG_ASSERT(commit.edits[e].value.is_short());
                CZ_DEBUG_ASSERT(commit.edits[e].value.len() == len);

                Edit edit;
                memcpy(edit.value.short_._buffer, commit.edits[e].value.short_._buffer, len);
                edit.value.short_._buffer[len] = code;
                edit.value.short_.set_len(len + 1);
                edit.position = commit.edits[e].position + e;
                edit.flags = Edit::INSERT;
                transaction.push(edit);
            }

            transaction.commit(buffer);

            return;
        }
    }

    insert_char(buffer, window, code);
}

static Command lookup_key_chain(Key_Map* map, size_t start, size_t* end, cz::Slice<Key> key_chain) {
    size_t index = start;
    for (; index < key_chain.len; ++index) {
        Key_Bind* bind = map->lookup(key_chain[index]);
        if (bind == nullptr) {
            ++index;
            *end = index;
            return command_insert_char;
        }

        if (bind->is_command) {
            ++index;
            *end = index;
            return bind->v.command;
        } else {
            map = bind->v.map;
        }
    }

    return nullptr;
}

static bool get_key_press_command(Editor* editor,
                                  Client* client,
                                  Key_Map* key_map,
                                  size_t* start,
                                  cz::Slice<Key> key_chain,
                                  Command* previous_command,
                                  bool* waiting_for_more_keys,
                                  Command_Source* source,
                                  Command* command) {
    size_t index = *start;
    *command = lookup_key_chain(key_map, *start, &index, key_chain);
    if (*command && *command != command_insert_char) {
        source->client = client;
        source->keys = {client->key_chain.start() + *start, index - *start};
        source->previous_command = *previous_command;

        *previous_command = *command;
        *start = index;
        return true;
    } else {
        if (index == *start) {
            *waiting_for_more_keys = true;
        }

        return false;
    }
}

static bool handle_key_press(Editor* editor,
                             Client* client,
                             Key_Map* key_map,
                             size_t* start,
                             cz::Slice<Key> key_chain,
                             Command* previous_command,
                             bool* waiting_for_more_keys) {
    Command command;
    Command_Source source;
    if (get_key_press_command(editor, client, key_map, start, key_chain, previous_command,
                              waiting_for_more_keys, &source, &command)) {
        command(editor, source);
        return true;
    } else {
        return false;
    }
}

static void failed_key_press(Editor* editor,
                             Client* client,
                             Command* previous_command,
                             size_t start) {
    Key key = client->key_chain[start];
    if (key.modifiers == 0 && (isprint(key.code) || key.code == '\t' || key.code == '\n')) {
        if (key.code == '\n' && client->selected_window() == client->mini_buffer_window()) {
            send_message_result(editor, client);
            *previous_command = nullptr;
        } else {
            Command_Source source;
            source.client = client;
            source.keys = {client->key_chain.start() + start, 1};
            source.previous_command = *previous_command;

            command_insert_char(editor, source);
            *previous_command = command_insert_char;
        }
    } else {
        client->show_message("Invalid key combo");
        *previous_command = nullptr;
    }
}

static bool handle_key_press_buffer(Editor* editor,
                                    Client* client,
                                    size_t* start,
                                    cz::Slice<Key> key_chain,
                                    Command* previous_command,
                                    bool* waiting_for_more_keys) {
    Buffer_Handle* handle = editor->lookup(client->selected_window()->id);
    Buffer* buffer = handle->lock();
    bool unlocked = false;
    CZ_DEFER(if (!unlocked) handle->unlock());

    if (buffer->mode.key_map) {
        Command command;
        Command_Source source;
        if (get_key_press_command(editor, client, buffer->mode.key_map, start, key_chain,
                                  previous_command, waiting_for_more_keys, &source, &command)) {
            unlocked = true;
            handle->unlock();
            command(editor, source);
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

void Server::receive(Client* client, Key key) {
    ZoneScoped;

    client->key_chain.reserve(cz::heap_allocator(), 1);
    client->key_chain.push(key);

    cz::Slice<Key> key_chain = client->key_chain;
    size_t start = 0;
    while (start < key_chain.len) {
        bool waiting_for_more_keys = false;

        if (handle_key_press_buffer(&editor, client, &start, key_chain, &previous_command,
                                    &waiting_for_more_keys)) {
            continue;
        }

        if (handle_key_press(&editor, client, &editor.key_map, &start, key_chain, &previous_command,
                             &waiting_for_more_keys)) {
            continue;
        }

        if (waiting_for_more_keys) {
            break;
        }

        failed_key_press(&editor, client, &previous_command, start);
        ++start;
    }

    client->key_chain.remove_range(0, start);
}

}
