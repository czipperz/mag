#include "server.hpp"

#include <ctype.h>
#include <Tracy.hpp>
#include <cz/heap.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "custom/config.hpp"
#include "insert.hpp"

namespace mag {

Client Server::make_client() {
    Client client = {};
    Buffer_Id selected_buffer_id = {editor.buffers.len() - 1};

    Buffer mini_buffer = {};
    mini_buffer.type = Buffer::TEMPORARY;
    mini_buffer.name = cz::Str("*client mini buffer*").duplicate(cz::heap_allocator());
    Buffer_Id mini_buffer_id = editor.create_buffer(mini_buffer);
    client.init(selected_buffer_id, mini_buffer_id);
    return client;
}

static bool is_word_char(char c) {
    return isalnum(c) || c == '_';
}
static bool can_merge_insert(cz::Str str, char code) {
    char last = str[str.len - 1];
    return is_word_char(last) && is_word_char(code);
}

static void command_insert_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    char code = source.keys[0].code;

    if (buffer->check_last_committer(command_insert_char, window->cursors)) {
        CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len());
        Commit commit = buffer->commits[buffer->commit_index - 1];
        size_t len = commit.edits[0].value.len();
        if (len < SSOStr::MAX_SHORT_LEN && can_merge_insert(commit.edits[0].value.as_str(), code)) {
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

            transaction.commit(buffer, command_insert_char);

            return;
        }
    }

    insert_char(buffer, window, code, command_insert_char);
}

static Command lookup_key_chain(Key_Map* map, size_t start, size_t* end, cz::Slice<Key> key_chain) {
    size_t index = start;
    for (; index < key_chain.len; ++index) {
        Key_Bind* bind = map->lookup(key_chain[index]);
        if (bind == nullptr) {
            ++index;
            *end = index;
            return {command_insert_char, "command_insert_char"};
        }

        if (bind->is_command) {
            ++index;
            *end = index;
            return bind->v.command;
        } else {
            map = bind->v.map;
        }
    }

    return {};
}

static bool get_key_press_command(Client* client,
                                  Key_Map* key_map,
                                  size_t* start,
                                  cz::Slice<Key> key_chain,
                                  Command* previous_command,
                                  bool* waiting_for_more_keys,
                                  Command_Source* source,
                                  Command* command) {
    size_t index = *start;
    *command = lookup_key_chain(key_map, *start, &index, key_chain);
    if (command->function && command->function != command_insert_char) {
        source->client = client;
        source->keys = {client->key_chain.start() + *start, index - *start};
        source->previous_command = previous_command->function;

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

#ifndef NDEBUG
struct File_Wrapper {
    FILE* file;

    File_Wrapper(const char* f, const char* m) { file = fopen(f, m); }

    ~File_Wrapper() {
        if (file) {
            fclose(file);
        }
    }
};
#endif

static void run_command(Command command, Editor* editor, Command_Source source) {
    try {
        command.function(editor, source);
    } catch (std::exception& ex) {
        source.client->show_message(ex.what());
    }

#ifndef NDEBUG
    static File_Wrapper log("log.txt", "a");
    if (log.file) {
        fprintf(log.file, "%s\n", command.string);
    }
#endif
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
    if (get_key_press_command(client, key_map, start, key_chain, previous_command,
                              waiting_for_more_keys, &source, &command)) {
        run_command(command, editor, source);
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
        Command_Source source;
        source.client = client;
        source.keys = {client->key_chain.start() + start, 1};
        source.previous_command = previous_command->function;

        Command command = {command_insert_char, "command_insert_char"};
        run_command(command, editor, source);
        *previous_command = command;
    } else {
        client->show_message("Invalid key combo");
        *previous_command = {};
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
        if (get_key_press_command(client, buffer->mode.key_map, start, key_chain, previous_command,
                                  waiting_for_more_keys, &source, &command)) {
            unlocked = true;
            handle->unlock();
            run_command(command, editor, source);
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

static bool handle_key_press_completion(Editor* editor,
                                        Client* client,
                                        size_t* start,
                                        cz::Slice<Key> key_chain,
                                        Command* previous_command,
                                        bool* waiting_for_more_keys) {
    Window_Unified* window = client->selected_window();
    if (!window->completing) {
        return false;
    }

    Command command;
    Command_Source source;
    if (get_key_press_command(client, custom::window_completion_key_map(), start, key_chain,
                              previous_command, waiting_for_more_keys, &source, &command)) {
        run_command(command, editor, source);
        return true;
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

        if (handle_key_press_completion(&editor, client, &start, key_chain, &previous_command,
                                        &waiting_for_more_keys)) {
            continue;
        }

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
