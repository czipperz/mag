#include "buffer_commands.hpp"

#include <stdlib.h>
#include <cz/working_directory.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "window_commands.hpp"

namespace mag {
namespace basic {

static void command_open_file_callback(Editor* editor, Client* client, cz::Str query, void* data) {
    {
        WITH_CONST_SELECTED_BUFFER(client);
        push_jump(window, client, buffer);
    }

    open_file(editor, client, query);
}

void command_open_file(Editor* editor, Command_Source source) {
    source.client->show_dialog(editor, "Open file: ", file_completion_engine,
                               command_open_file_callback, nullptr);

    fill_mini_buffer_with_selected_window_directory(editor, source.client);
}

void fill_mini_buffer_with_selected_window_directory(Editor* editor, Client* client) {
    cz::String default_value = {};
    CZ_DEFER(default_value.drop(cz::heap_allocator()));

    {
        WITH_CONST_WINDOW_BUFFER(client->selected_normal_window);
        if (buffer->directory.len() > 0) {
            default_value = buffer->directory.clone(cz::heap_allocator());
        }
    }

    if (default_value.len() == 0) {
        if (cz::get_working_directory(cz::heap_allocator(), &default_value).is_err()) {
            return;
        }
        default_value.reserve(cz::heap_allocator(), 1);
        default_value.push('/');
    }

    fill_mini_buffer_with(editor, client, default_value);
}

void fill_mini_buffer_with(Editor* editor, Client* client, cz::Str default_value) {
    Window_Unified* window = client->mini_buffer_window();
    WITH_WINDOW_BUFFER(window);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    Edit edit;
    edit.value = SSOStr::as_duplicate(transaction.value_allocator(), default_value);
    edit.position = 0;
    edit.flags = Edit::INSERT;
    transaction.push(edit);

    transaction.commit();
}

void command_save_file(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (buffer->type != Buffer::FILE) {
        source.client->show_message(editor, "Buffer must be associated with a file");
        return;
    }

    if (!save_buffer(buffer)) {
        source.client->show_message(editor, "Error saving file");
    }
}

static void command_switch_buffer_callback(Editor* editor,
                                           Client* client,
                                           cz::Str path,
                                           void* data) {
    cz::Arc<Buffer_Handle> handle;
    if (path.starts_with("*")) {
        if (find_temp_buffer(editor, client, path, &handle)) {
            goto cont;
        }
    }

    if (!find_buffer_by_path(editor, client, path, &handle)) {
        client->show_message(editor, "Couldn't find the buffer to switch to");
        return;
    }

    {
    cont:
        WITH_CONST_SELECTED_BUFFER(client);
        push_jump(window, client, buffer);
    }

    client->set_selected_buffer(handle->id);
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
            window->second->parent = window->parent;
            Window_Split::drop_non_recursive(window);

            if (left_matches == 2) {
                *selected_window = window_first(*w);
            }

            return 0;
        } else if (right_matches) {
            Window::drop_(window->second);

            *w = window->first;
            window->first->parent = window->parent;
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
        cz::Arc<Buffer_Handle> handle;
        if (!find_buffer_by_path(editor, client, path, &handle)) {
            client->show_message(editor, "Couldn't find the buffer to kill");
            return;
        }
        buffer_id = handle->id;
    }

    // TODO: prevent killing *scratch*
    editor->kill(buffer_id);

    remove_windows_for_buffer(client, buffer_id, editor->buffers[0]->id);
}

void command_kill_buffer(Editor* editor, Command_Source source) {
    Buffer_Id* buffer_id = cz::heap_allocator().alloc<Buffer_Id>();
    CZ_ASSERT(buffer_id);
    *buffer_id = source.client->selected_window()->id;
    source.client->show_dialog(editor, "Buffer to kill: ", buffer_completion_engine,
                               command_kill_buffer_callback, buffer_id);
}

static void command_rename_buffer_callback(Editor* editor, Client* client, cz::Str path, void* data) {
    Buffer_Id* buffer_id = (Buffer_Id*)data;
    CZ_DEFER(cz::heap_allocator().dealloc(buffer_id));

    cz::Str name;
    cz::Str directory;
    parse_rendered_buffer_name(path, &name, &directory);

    cz::String name_clone = name.duplicate(cz::heap_allocator());
    CZ_DEFER(name_clone.drop(cz::heap_allocator()));
    cz::String directory_clone = directory.duplicate(cz::heap_allocator());
    CZ_DEFER(directory_clone.drop(cz::heap_allocator()));

    WITH_BUFFER(*buffer_id);
    std::swap(buffer->name, name_clone);
    std::swap(buffer->directory, directory_clone);
}

void command_rename_buffer(Editor* editor, Command_Source source) {
    Buffer_Id* buffer_id = cz::heap_allocator().alloc<Buffer_Id>();
    CZ_ASSERT(buffer_id);
    *buffer_id = source.client->selected_window()->id;

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_BUFFER(*buffer_id);
        if (buffer->type == Buffer::TEMPORARY) {
            source.client->show_dialog(editor, "Rename buffer to: ", no_completion_engine,
                                       command_rename_buffer_callback, buffer_id);
        } else {
            source.client->show_dialog(editor, "Rename buffer to: ", file_completion_engine,
                                       command_rename_buffer_callback, buffer_id);
        }

        buffer->render_name(cz::heap_allocator(), &path);
    }

    fill_mini_buffer_with(editor, source.client, path);
}

}
}
