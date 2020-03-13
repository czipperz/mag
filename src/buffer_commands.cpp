#include "buffer_commands.hpp"

#include <stdlib.h>
#include "command_macros.hpp"
#include "file.hpp"

namespace mag {

static void command_open_file_callback(Editor* editor, Client* client, cz::Str query, void* data) {
    open_file(editor, client, query);
}

void command_open_file(Editor* editor, Command_Source source) {
    Message message = {};
    message.tag = Message::RESPOND_FILE;
    message.text = "Open file: ";
    message.response_callback = command_open_file_callback;

    cz::String default_value = {};
    CZ_DEFER(default_value.drop(cz::heap_allocator()));
    bool has_default_value;
    WITH_SELECTED_BUFFER({
        has_default_value = buffer->path.find('/') != nullptr;
        if (has_default_value) {
            default_value = buffer->path.clone(cz::heap_allocator());
        }
    });

    if (has_default_value) {
        WITH_BUFFER(buffer, source.client->mini_buffer_id(), WITH_TRANSACTION({
                        transaction.init(1, default_value.len());
                        Edit edit;
                        edit.value.init_duplicate(transaction.value_allocator(), default_value);
                        edit.position = 0;
                        edit.is_insert = true;
                        transaction.push(edit);
                    }));
    }

    source.client->show_message(message);
}

void command_save_file(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        if (!buffer->path.find('/')) {
            Message message = {};
            message.tag = Message::SHOW;
            message.text = "File must have path";
            source.client->show_message(message);
            return;
        }

        if (!save_contents(&buffer->contents, buffer->path.buffer())) {
            Message message = {};
            message.tag = Message::SHOW;
            message.text = "Error saving file";
            source.client->show_message(message);
            return;
        }

        buffer->mark_saved();
    });
}

static void command_kill_buffer_callback(Editor* editor, Client* client, cz::Str path, void* data) {
    Buffer_Id buffer_id;
    if (path.len == 0) {
        buffer_id = *(Buffer_Id*)data;
    } else {
        if (!find_buffer_by_path(editor, client, path, &buffer_id)) {
            return;
        }
    }
}

void command_kill_buffer(Editor* editor, Command_Source source) {
    Message message = {};
    message.tag = Message::RESPOND_BUFFER;
    message.text = "Buffer to kill";
    message.response_callback = command_kill_buffer_callback;

    Buffer_Id* buffer_id = (Buffer_Id*)malloc(sizeof(Buffer_Id));
    *buffer_id = source.client->selected_buffer_id();
    message.response_callback_data = buffer_id;

    source.client->show_message(message);
}

}
