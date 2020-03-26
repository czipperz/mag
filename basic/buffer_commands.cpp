#include "buffer_commands.hpp"

#include <stdlib.h>
#include "command_macros.hpp"
#include "file.hpp"
#include "window_commands.hpp"

namespace mag {
namespace basic {

static void command_open_file_callback(Editor* editor, Client* client, cz::Str query, void* data) {
    open_file(editor, client, query);
}

void command_open_file(Editor* editor, Command_Source source) {
    source.client->show_dialog(editor, "Open file: ", file_completion_engine,
                               command_open_file_callback, nullptr);

    cz::String default_value = {};
    CZ_DEFER(default_value.drop(cz::heap_allocator()));
    bool has_default_value;
    {
        WITH_WINDOW_BUFFER(source.client->selected_normal_window);
        has_default_value = buffer->path.find('/') != nullptr;
        if (has_default_value) {
            default_value = buffer->path.clone(cz::heap_allocator());
        }
    }

    if (has_default_value) {
        Window_Unified* window = source.client->mini_buffer_window();
        WITH_WINDOW_BUFFER(window);

        Transaction transaction;
        transaction.init(1, default_value.len());
        CZ_DEFER(transaction.drop());

        Edit edit;
        edit.value.init_duplicate(transaction.value_allocator(), default_value);
        edit.position = 0;
        edit.flags = Edit::INSERT;
        transaction.push(edit);

        transaction.commit(buffer);
    }
}

void command_save_file(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (!buffer->path.find('/')) {
        source.client->show_message("File must have path");
        return;
    }

    if (!save_contents(&buffer->contents, buffer->path.buffer())) {
        source.client->show_message("Error saving file");
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
    source.client->show_dialog(editor, "Buffer to switch to: ", buffer_completion_engine,
                               command_switch_buffer_callback, nullptr);
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
    Buffer_Id* buffer_id = (Buffer_Id*)malloc(sizeof(Buffer_Id));
    *buffer_id = source.client->selected_window()->id;
    source.client->show_dialog(editor, "Buffer to kill: ", buffer_completion_engine,
                               command_kill_buffer_callback, buffer_id);
}

}
}
