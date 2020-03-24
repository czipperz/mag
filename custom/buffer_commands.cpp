#include "buffer_commands.hpp"

#include <stdlib.h>
#include "command_macros.hpp"
#include "file.hpp"
#include "window_commands.hpp"

namespace mag {
namespace custom {

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
    {
        WITH_SELECTED_BUFFER();
        has_default_value = buffer->path.find('/') != nullptr;
        if (has_default_value) {
            default_value = buffer->path.clone(cz::heap_allocator());
        }
    }

    if (has_default_value) {
        WITH_BUFFER(source.client->mini_buffer_window()->id);
        Transaction transaction;
        transaction.init(1, default_value.len());
        CZ_DEFER(transaction.drop());

        Edit edit;
        edit.value.init_duplicate(transaction.value_allocator(), default_value);
        edit.position = 0;
        edit.is_insert = true;
        transaction.push(edit);

        transaction.commit(buffer);
    }

    source.client->show_message(message);
}

void command_save_file(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER();

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
}

static void command_switch_buffer_callback(Editor* editor,
                                           Client* client,
                                           cz::Str path,
                                           void* data) {
    Buffer_Id buffer_id;
    if (!find_buffer_by_path(editor, client, path, &buffer_id)) {
        return;
    }

    client->set_selected_buffer(buffer_id);
}

void command_switch_buffer(Editor* editor, Command_Source source) {
    Message message = {};
    message.tag = Message::RESPOND_BUFFER;
    message.text = "Buffer to switch to: ";
    message.response_callback = command_switch_buffer_callback;

    source.client->show_message(message);
}

static int remove_windows_matching(Window** w, Buffer_Id id, Window_Unified** selected_window) {
    switch ((*w)->tag) {
    case Window::UNIFIED: {
        Window_Unified* window = (Window_Unified*)*w;
        if (id == window->id) {
            if (id == (*selected_window)->id) {
                return 2;
            } else {
                return 1;
            }
        } else {
            return 0;
        }
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)*w;
        int left_matches = remove_windows_matching(&window->first, id, selected_window);
        int right_matches = remove_windows_matching(&window->second, id, selected_window);
        if (left_matches && right_matches) {
            return cz::max(left_matches, right_matches);
        } else if (left_matches) {
            // Don't put the windows into Client::offscreen_windows because the buffer is killed
            Window::drop_(window->first);

            *w = window->second;
            Window_Split::drop_non_recursive(window);

            if (left_matches == 2) {
                *selected_window = window_first(*w);
            }

            return 0;
        } else if (right_matches) {
            Window::drop_(window->second);

            *w = window->first;
            Window_Split::drop_non_recursive(window);

            if (right_matches == 2) {
                *selected_window = window_first(*w);
            }

            return 0;
        } else {
            return 0;
        }
    }
    }

    CZ_PANIC("");
}

void remove_windows_for_buffer(Client* client, Buffer_Id buffer_id, Buffer_Id replacement_id) {
    if (remove_windows_matching(&client->window, buffer_id, &client->selected_normal_window)) {
        // Every buffer matches the killed buffer id
        Window::drop_(client->window);
        client->selected_normal_window = Window_Unified::create(replacement_id);
        client->window = client->selected_normal_window;
    }
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

    // TODO: prevent killing *scratch*
    editor->kill(buffer_id);

    remove_windows_for_buffer(client, buffer_id, editor->buffers[0]->id);
}

void command_kill_buffer(Editor* editor, Command_Source source) {
    Message message = {};
    message.tag = Message::RESPOND_BUFFER;
    message.text = "Buffer to kill: ";
    message.response_callback = command_kill_buffer_callback;

    Buffer_Id* buffer_id = (Buffer_Id*)malloc(sizeof(Buffer_Id));
    *buffer_id = source.client->selected_window()->id;
    message.response_callback_data = buffer_id;

    source.client->show_message(message);
}

}
}
