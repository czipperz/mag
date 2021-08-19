#include "buffer_commands.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <cz/char_type.hpp>
#include <cz/file.hpp>
#include <cz/format.hpp>
#include <cz/heap_string.hpp>
#include <cz/path.hpp>
#include <cz/working_directory.hpp>
#include "command_macros.hpp"
#include "config.hpp"
#include "file.hpp"
#include "syntax/tokenize_buffer_name.hpp"
#include "syntax/tokenize_path.hpp"
#include "window_commands.hpp"

namespace mag {
namespace basic {

static void reset_mode(Editor* editor, Buffer* buffer) {
    buffer->mode.drop();
    buffer->mode = {};

    buffer->token_cache.reset();

    custom::buffer_created_callback(editor, buffer);
}

static void command_open_file_callback(Editor* editor, Client* client, cz::Str query, void* data) {
    {
        WITH_CONST_SELECTED_BUFFER(client);
        push_jump(window, client, buffer);
    }

    open_file(editor, client, query);
}

void command_open_file(Editor* editor, Command_Source source) {
    cz::String selected_window_directory = {};
    CZ_DEFER(selected_window_directory.drop(cz::heap_allocator()));
    get_selected_window_directory(editor, source.client, cz::heap_allocator(),
                                  &selected_window_directory);

    Dialog dialog = {};
    dialog.prompt = "Open file: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_open_file_callback;
    dialog.mini_buffer_contents = selected_window_directory;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(editor, dialog);
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

static void command_save_file_callback(Editor* editor, Client* client, cz::Str, void*) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    cz::Vector<size_t> stack = {};
    CZ_DEFER(stack.drop(cz::heap_allocator()));

    WITH_SELECTED_BUFFER(client);

    // This shouldn't happen unless the user switches which buffer they select mid prompt.
    if (buffer->directory.len() == 0) {
        return;
    }

    directory = buffer->directory.clone(cz::heap_allocator());
    CZ_ASSERT(directory.ends_with('/'));
    directory.pop();

    // Find the first existing directory and track all directories we need to create.
    while (1) {
        // If we have hit the root then stop and create from there.
#ifdef _WIN32
        if (directory.len() == 2) {
            if (cz::is_alpha(directory[0]) && directory[1] == ':') {
                break;
            }
        }
#else
        if (directory.len() == 0) {
            break;
        }
#endif

        // If this part of the path exists then we can start creating from this point.
        directory.null_terminate();
        if (cz::file::exists(directory.buffer())) {
            break;
        }

        stack.reserve(cz::heap_allocator(), 1);
        stack.push(directory.len());
        if (!cz::path::pop_component(&directory)) {
            break;
        }
    }

    // Create all the directories.
    for (size_t i = stack.len(); i-- > 0;) {
        directory.push('/');
        directory.set_len(stack[i]);

        // Should be null terminated via the loop above.
        CZ_DEBUG_ASSERT(*directory.end() == '\0');

        if (!cz::file::create_directory(directory.buffer())) {
            client->show_message(editor, "Failed to create parent directory");
        }
    }

    if (!save_buffer(buffer)) {
        client->show_message(editor, "Error saving file");
    }
}

void command_save_file(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (buffer->type != Buffer::FILE) {
        source.client->show_message(editor, "Buffer must be associated with a file");
        return;
    }

    if (!cz::file::exists(buffer->directory.buffer())) {
        Dialog dialog = {};
        dialog.prompt = "Submit to confirm create directory ";
        dialog.response_callback = command_save_file_callback;
        source.client->show_dialog(editor, dialog);
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

    client->set_selected_buffer(handle);
}

void command_switch_buffer(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Buffer to switch to: ";
    dialog.completion_engine = buffer_completion_engine;
    dialog.response_callback = command_switch_buffer_callback;
    dialog.next_token = syntax::buffer_name_next_token;
    source.client->show_dialog(editor, dialog);
}

static int remove_windows_matching(Window** w,
                                   cz::Arc<Buffer_Handle> buffer_handle,
                                   Window_Unified** selected_window) {
    switch ((*w)->tag) {
    case Window::UNIFIED: {
        Window_Unified* window = (Window_Unified*)*w;
        if (buffer_handle.get() == window->buffer_handle.get()) {
            if (buffer_handle.get() == (*selected_window)->buffer_handle.get()) {
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
        int left_matches = remove_windows_matching(&window->first, buffer_handle, selected_window);
        int right_matches =
            remove_windows_matching(&window->second, buffer_handle, selected_window);
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

void remove_windows_for_buffer(Client* client,
                               cz::Arc<Buffer_Handle> buffer_handle,
                               cz::Arc<Buffer_Handle> replacement) {
    if (remove_windows_matching(&client->window, buffer_handle, &client->selected_normal_window)) {
        // Every buffer matches the killed buffer id
        Window::drop_(client->window);
        client->selected_normal_window = Window_Unified::create(replacement);
        client->window = client->selected_normal_window;
    }

    cz::Vector<Window_Unified*>& offscreen_windows = client->_offscreen_windows;
    for (size_t i = offscreen_windows.len(); i-- > 0;) {
        if (offscreen_windows[i]->buffer_handle.get() == buffer_handle.get()) {
            Window::drop_(offscreen_windows[i]);
            offscreen_windows.remove(i);
            break;
        }
    }
}

static void command_kill_buffer_callback(Editor* editor, Client* client, cz::Str path, void*) {
    cz::Arc<Buffer_Handle> buffer_handle;
    if (path.len == 0) {
        buffer_handle = client->selected_normal_window->buffer_handle;
    } else {
        if (!find_buffer_by_path(editor, client, path, &buffer_handle)) {
            client->show_message(editor, "Couldn't find the buffer to kill");
            return;
        }
    }

    // Prevent killing special buffers (*scratch*, *splash
    // page*, *client messages*, and *client mini buffer*).
    {
        WITH_CONST_BUFFER_HANDLE(buffer_handle);
        if (buffer->id.value < 4) {
            return;
        }
    }

    editor->kill(buffer_handle.get());

    remove_windows_for_buffer(client, buffer_handle, editor->buffers[0]);
}

void command_kill_buffer(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Buffer to kill: ";
    dialog.completion_engine = buffer_completion_engine;
    dialog.response_callback = command_kill_buffer_callback;
    dialog.next_token = syntax::buffer_name_next_token;
    source.client->show_dialog(editor, dialog);
}

static void command_rename_buffer_callback(Editor* editor,
                                           Client* client,
                                           cz::Str path,
                                           void* data) {
    cz::Str name;
    cz::Str directory;
    Buffer::Type type = parse_rendered_buffer_name(path, &name, &directory);

    cz::String name_clone = name.clone(cz::heap_allocator());
    CZ_DEFER(name_clone.drop(cz::heap_allocator()));
    cz::String directory_clone = directory.clone_null_terminate(cz::heap_allocator());
    CZ_DEFER(directory_clone.drop(cz::heap_allocator()));

    WITH_SELECTED_BUFFER(client);
    std::swap(buffer->name, name_clone);
    std::swap(buffer->directory, directory_clone);
    buffer->type = type;
}

void command_rename_buffer(Editor* editor, Command_Source source) {
    bool is_temporary;
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        is_temporary = buffer->type == Buffer::TEMPORARY;
        buffer->render_name(cz::heap_allocator(), &path);
    }

    Dialog dialog = {};
    dialog.prompt = "Rename buffer to: ";
    dialog.completion_engine = is_temporary ? no_completion_engine : file_completion_engine;
    dialog.response_callback = command_rename_buffer_callback;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(editor, dialog);

    fill_mini_buffer_with(editor, source.client, path);
}

static void command_save_buffer_to_callback(Editor* editor,
                                            Client* client,
                                            cz::Str path,
                                            void* data) {
    cz::Str name;
    cz::Str directory;
    Buffer::Type type = parse_rendered_buffer_name(path, &name, &directory);
    if (type != Buffer::FILE) {
        client->show_message(editor, "Buffer name must be for a file");
        return;
    }

    cz::String name_clone = name.clone(cz::heap_allocator());
    CZ_DEFER(name_clone.drop(cz::heap_allocator()));
    cz::String directory_clone = directory.clone_null_terminate(cz::heap_allocator());
    CZ_DEFER(directory_clone.drop(cz::heap_allocator()));

    WITH_SELECTED_BUFFER(client);
    std::swap(buffer->name, name_clone);
    std::swap(buffer->directory, directory_clone);
    buffer->type = type;

    // Force the buffer to be unsaved.
    buffer->saved_commit_id = {{(uint64_t)-1}};

    if (!save_buffer(buffer)) {
        client->show_message(editor, "Error saving file");
    }
}

void command_save_buffer_to(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        path.reserve(cz::heap_allocator(), buffer->directory.len() + buffer->name.len());
        path.append(buffer->directory);
        path.append(buffer->name);
    }

    Dialog dialog = {};
    dialog.prompt = "Save buffer to: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_save_buffer_to_callback;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(editor, dialog);

    fill_mini_buffer_with(editor, source.client, path);
}

static void command_pretend_rename_buffer_callback(Editor* editor,
                                                   Client* client,
                                                   cz::Str path,
                                                   void* data) {
    WITH_SELECTED_BUFFER(client);

    cz::Str name, directory;
    Buffer::Type type = parse_rendered_buffer_name(path, &name, &directory);

    Buffer::Type btype = buffer->type;
    buffer->type = type;
    CZ_DEFER(buffer->type = btype);

    cz::String bname = buffer->name;
    buffer->name = name.clone(cz::heap_allocator());
    CZ_DEFER(buffer->name = bname);
    CZ_DEFER(buffer->name.drop(cz::heap_allocator()));

    cz::String bdirectory = buffer->directory;
    buffer->directory = directory.clone(cz::heap_allocator());
    CZ_DEFER(buffer->directory = bdirectory);
    CZ_DEFER(buffer->directory.drop(cz::heap_allocator()));

    reset_mode(editor, buffer);
}

void command_pretend_rename_buffer(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        path.reserve(cz::heap_allocator(), buffer->directory.len() + buffer->name.len());
        path.append(buffer->directory);
        path.append(buffer->name);
    }

    Dialog dialog = {};
    dialog.prompt = "Pretend rename buffer to: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_pretend_rename_buffer_callback;
    dialog.next_token = syntax::path_next_token;
    dialog.mini_buffer_contents = path;
    source.client->show_dialog(editor, dialog);
}

static void command_diff_buffer_against_callback(Editor* editor,
                                                 Client* client,
                                                 cz::Str path,
                                                 void* data) {
    WITH_CONST_SELECTED_BUFFER(client);

    char temp_file_buffer[L_tmpnam];
    if (!tmpnam(temp_file_buffer)) {
        client->show_message(editor, "No temporary file available");
        return;
    }

    if (!save_buffer_to(buffer, temp_file_buffer)) {
        client->show_message(editor, "Couldn't save to temporary file");
        return;
    }

    cz::Heap_String name = {};
    CZ_DEFER(name.drop());
    name = cz::format("diff ", path, " ");
    buffer->render_name(cz::heap_allocator(), &name);

    cz::Str args[] = {"diff", path, temp_file_buffer};

    run_console_command(client, editor, buffer->directory.buffer(), args, name, "Diff error");
}

void command_diff_buffer_against(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        path.reserve(cz::heap_allocator(), buffer->directory.len() + buffer->name.len());
        path.append(buffer->directory);
        if (buffer->type == Buffer::FILE) {
            path.append(buffer->name);
        }
    }

    Dialog dialog = {};
    dialog.prompt = "Diff buffer against: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_diff_buffer_against_callback;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(editor, dialog);

    fill_mini_buffer_with(editor, source.client, path);
}

}
}
