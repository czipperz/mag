#include "buffer_commands.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <cz/char_type.hpp>
#include <cz/file.hpp>
#include <cz/format.hpp>
#include <cz/heap_string.hpp>
#include <cz/path.hpp>
#include <cz/process.hpp>
#include <cz/util.hpp>
#include <cz/working_directory.hpp>
#include "core/command_macros.hpp"
#include "core/diff.hpp"
#include "core/file.hpp"
#include "custom/config.hpp"
#include "syntax/tokenize_buffer_name.hpp"
#include "syntax/tokenize_path.hpp"
#include "window_commands.hpp"

namespace mag {
namespace basic {

void reset_mode(Editor* editor, Buffer* buffer, const cz::Arc<Buffer_Handle>& buffer_handle) {
    Tokenizer old_next_token = buffer->mode.next_token;
    buffer->mode.drop();
    buffer->mode = {};

    custom::buffer_created_callback(editor, buffer, buffer_handle);

    if (buffer->mode.next_token != old_next_token) {
        buffer->token_cache.reset(buffer);
        editor->add_asynchronous_job(job_syntax_highlight_buffer(buffer_handle.clone_downgrade()));
    }
}

void reset_mode_as_if(Editor* editor,
                      Buffer* buffer,
                      const cz::Arc<Buffer_Handle>& buffer_handle,
                      cz::Str name,
                      cz::Str directory,
                      Buffer::Type type) {
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

    reset_mode(editor, buffer, buffer_handle);
}

bool reset_mode_as_if_named(Editor* editor,
                            Buffer* buffer,
                            const cz::Arc<Buffer_Handle>& buffer_handle,
                            cz::Str path) {
    cz::Str name, directory;
    Buffer::Type type;
    if (!parse_rendered_buffer_name(path, &name, &directory, &type)) {
        return false;
    }
    reset_mode_as_if(editor, buffer, buffer_handle, name, directory, type);
    return true;
}

static void command_open_file_callback(Editor* editor, Client* client, cz::Str query, void* data) {
    {
        WITH_CONST_SELECTED_BUFFER(client);
        push_jump(window, client, buffer);
    }

    open_file_arg(editor, client, query);
}

REGISTER_COMMAND(command_open_file);
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
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_open_file_full_path);
void command_open_file_full_path(Editor* editor, Command_Source source) {
    cz::String selected_window_path = {};
    CZ_DEFER(selected_window_path.drop(cz::heap_allocator()));
    get_selected_window_directory(editor, source.client, cz::heap_allocator(),
                                  &selected_window_path);

    {
        WITH_CONST_WINDOW_BUFFER(source.client->selected_normal_window, source.client);
        selected_window_path.reserve_exact(cz::heap_allocator(), buffer->name.len + 1);
        selected_window_path.append(buffer->name);
        selected_window_path.null_terminate();
    }

    Dialog dialog = {};
    dialog.prompt = "Open file: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_open_file_callback;
    dialog.mini_buffer_contents = selected_window_path;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(dialog);
}

static void command_save_file_callback(Editor* editor, Client* client, cz::Str, void*) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    cz::Vector<size_t> stack = {};
    CZ_DEFER(stack.drop(cz::heap_allocator()));

    WITH_SELECTED_BUFFER(client);

    // This shouldn't happen unless the user switches which buffer they select mid prompt.
    if (buffer->directory.len == 0) {
        return;
    }

    directory = buffer->directory.clone(cz::heap_allocator());
    CZ_ASSERT(directory.ends_with('/'));
    directory.pop();

    // Find the first existing directory and track all directories we need to create.
    while (1) {
        // If we have hit the root then stop and create from there.
#ifdef _WIN32
        if (directory.len == 2) {
            if (cz::is_alpha(directory[0]) && directory[1] == ':') {
                break;
            }
        }
#else
        if (directory.len == 0) {
            break;
        }
#endif

        // If this part of the path exists then we can start creating from this point.
        directory.null_terminate();
        if (cz::file::exists(directory.buffer)) {
            break;
        }

        stack.reserve(cz::heap_allocator(), 1);
        stack.push(directory.len);
        if (!cz::path::pop_component(&directory)) {
            break;
        }
    }

    // Create all the directories.
    for (size_t i = stack.len; i-- > 0;) {
        directory.push('/');
        directory.len = stack[i];

        // Should be null terminated via the loop above.
        CZ_DEBUG_ASSERT(*directory.end() == '\0');

        if (cz::file::create_directory(directory.buffer) != 0) {
            client->show_message("Failed to create parent directory");
        }
    }

    if (!save_buffer(buffer)) {
        client->show_message("Error saving file");
    }
}

REGISTER_COMMAND(command_save_file);
void command_save_file(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (buffer->type != Buffer::FILE) {
        source.client->show_message("Buffer must be associated with a file");
        return;
    }

    if (!cz::file::exists(buffer->directory.buffer)) {
        Dialog dialog = {};
        dialog.prompt = "Submit to confirm create directory ";
        dialog.response_callback = command_save_file_callback;
        source.client->show_dialog(dialog);
        return;
    }

    if (!save_buffer(buffer)) {
        source.client->show_message("Error saving file");
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

    if (!find_buffer_by_path(editor, path, &handle)) {
        client->show_message("Couldn't find the buffer to switch to");
        return;
    }

    {
    cont:
        WITH_CONST_SELECTED_BUFFER(client);
        push_jump(window, client, buffer);
    }

    client->set_selected_buffer(handle);
}

REGISTER_COMMAND(command_switch_buffer);
void command_switch_buffer(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Buffer to switch to: ";
    dialog.completion_engine = buffer_completion_engine;
    dialog.response_callback = command_switch_buffer_callback;
    dialog.next_token = syntax::buffer_name_next_token;
    source.client->show_dialog(dialog);
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
            window->second->total_rows = window->total_rows;
            window->second->total_cols = window->total_cols;
            Window_Split::drop_non_recursive(window);

            if (left_matches == 2) {
                *selected_window = window_first_prefer_not_iterable(*w);
            }

            return 0;
        } else if (right_matches) {
            Window::drop_(window->second);

            *w = window->first;
            window->first->parent = window->parent;
            window->first->total_rows = window->total_rows;
            window->first->total_cols = window->total_cols;
            Window_Split::drop_non_recursive(window);

            if (right_matches == 2) {
                *selected_window = window_first_prefer_not_iterable(*w);
            }

            return 0;
        } else {
            return 0;
        }
    }
    }

    CZ_PANIC("");
}

static bool remove_windows_for_buffer(Client* client,
                                      cz::Arc<Buffer_Handle> buffer_handle,
                                      cz::Arc<Buffer_Handle> replacement) {
    bool everything = false;
    if (remove_windows_matching(&client->window, buffer_handle, &client->selected_normal_window)) {
        // Every buffer matches the killed buffer id
        everything = true;
        Window_Unified* win = Window_Unified::create(replacement, client->next_window_id++);
        win->total_rows = client->window->total_rows;
        win->total_cols = client->window->total_cols;
        Window::drop_(client->window);
        client->window = win;
        client->selected_normal_window = win;
    }

    cz::Vector<Window_Unified*>& offscreen_windows = client->_offscreen_windows;
    for (size_t i = offscreen_windows.len; i-- > 0;) {
        if (offscreen_windows[i]->buffer_handle.get() == buffer_handle.get()) {
            Window::drop_(offscreen_windows[i]);
            offscreen_windows.remove(i);
            break;
        }
    }
    return everything;
}

static bool run_kill(Editor* editor, Client* client, cz::Arc<Buffer_Handle> buffer_handle) {
    // Prevent killing special buffers (*scratch*, *splash
    // page*, *client messages*, and *client mini buffer*).
    {
        WITH_CONST_BUFFER_HANDLE(buffer_handle);
        if (buffer->id.value < 4) {
            return false;
        }
    }

    // @KillBuffer
    editor->kill(buffer_handle.get());

    if (remove_windows_for_buffer(client, buffer_handle, editor->buffers[0])) {
        (void)pop_jump(editor, client);
    }

    return true;
}

static void command_kill_buffer_callback(Editor* editor, Client* client, cz::Str path, void*) {
    cz::Arc<Buffer_Handle> buffer_handle;
    if (path.len == 0) {
        buffer_handle = client->selected_normal_window->buffer_handle;
    } else {
        if (!find_buffer_by_path(editor, path, &buffer_handle)) {
            client->show_message("Couldn't find the buffer to kill");
            return;
        }
    }

    run_kill(editor, client, buffer_handle);
}

REGISTER_COMMAND(command_kill_buffer);
void command_kill_buffer(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Buffer to kill: ";
    dialog.completion_engine = buffer_completion_engine;
    dialog.response_callback = command_kill_buffer_callback;
    dialog.next_token = syntax::buffer_name_next_token;
    source.client->show_dialog(dialog);
}

static void command_kill_buffers_in_folder_callback(Editor* editor,
                                                    Client* client,
                                                    cz::Str path_raw,
                                                    void*) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    if (path_raw.len == 0) {
        WITH_CONST_SELECTED_NORMAL_BUFFER(client);
        if (buffer->directory.len == 0) {
            client->show_message("Must specify directory to kill");
            return;
        }
        path = buffer->directory.clone(cz::heap_allocator());
    } else {
        path = standardize_path(cz::heap_allocator(), path_raw);
        if (!path.ends_with('/'))
            path.push('/');
    }

    for (size_t i = 0; i < editor->buffers.len;) {
        cz::Arc<Buffer_Handle> buffer_handle = editor->buffers[i];
        {
            WITH_CONST_BUFFER_HANDLE(buffer_handle);
            if (!buffer->directory.starts_with(path)) {
                ++i;
                continue;
            }
        }

        if (!run_kill(editor, client, buffer_handle))
            ++i;
    }
}

REGISTER_COMMAND(command_kill_buffers_in_folder);
void command_kill_buffers_in_folder(Editor* editor, Command_Source source) {
    cz::String selected_window_directory = {};
    CZ_DEFER(selected_window_directory.drop(cz::heap_allocator()));
    get_selected_window_directory(editor, source.client, cz::heap_allocator(),
                                  &selected_window_directory);

    Dialog dialog = {};
    dialog.prompt = "Folder to recursively kill: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_kill_buffers_in_folder_callback;
    dialog.mini_buffer_contents = selected_window_directory;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(dialog);
}

static void command_rename_buffer_callback(Editor* editor,
                                           Client* client,
                                           cz::Str path,
                                           void* data) {
    cz::Str name;
    cz::Str directory;
    Buffer::Type type;
    if (!parse_rendered_buffer_name(path, &name, &directory, &type)) {
        client->show_message("Error: invalid path");
        return;
    }

    cz::String name_clone = name.clone(cz::heap_allocator());
    CZ_DEFER(name_clone.drop(cz::heap_allocator()));
    cz::String directory_clone = directory.clone_null_terminate(cz::heap_allocator());
    CZ_DEFER(directory_clone.drop(cz::heap_allocator()));

    WITH_SELECTED_BUFFER(client);
    cz::swap(buffer->name, name_clone);
    cz::swap(buffer->directory, directory_clone);
    buffer->type = type;

    reset_mode(editor, buffer, handle);
}

REGISTER_COMMAND(command_rename_buffer);
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
    dialog.mini_buffer_contents = path;
    source.client->show_dialog(dialog);
}

static void command_save_buffer_to_callback(Editor* editor,
                                            Client* client,
                                            cz::Str path,
                                            void* data) {
    cz::Str name;
    cz::Str directory;
    Buffer::Type type;
    if (!parse_rendered_buffer_name(path, &name, &directory, &type)) {
        client->show_message("Error: invalid path");
        return;
    }
    if (type != Buffer::FILE) {
        client->show_message("Buffer name must be for a file");
        return;
    }

    cz::String name_clone = name.clone(cz::heap_allocator());
    CZ_DEFER(name_clone.drop(cz::heap_allocator()));
    cz::String directory_clone = directory.clone_null_terminate(cz::heap_allocator());
    CZ_DEFER(directory_clone.drop(cz::heap_allocator()));

    WITH_SELECTED_BUFFER(client);
    cz::swap(buffer->name, name_clone);
    cz::swap(buffer->directory, directory_clone);
    buffer->type = type;

    // Force the buffer to be unsaved.
    buffer->saved_commit_id = {{(uint64_t)-1}};

    if (!save_buffer(buffer)) {
        client->show_message("Error saving file");
    }

    reset_mode(editor, buffer, handle);
}

REGISTER_COMMAND(command_save_buffer_to);
void command_save_buffer_to(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        path.reserve(cz::heap_allocator(), buffer->directory.len + buffer->name.len);
        path.append(buffer->directory);
        path.append(buffer->name);
    }

    Dialog dialog = {};
    dialog.prompt = "Save buffer to: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_save_buffer_to_callback;
    dialog.next_token = syntax::path_next_token;
    dialog.mini_buffer_contents = path;
    source.client->show_dialog(dialog);
}

static void command_pretend_rename_buffer_callback(Editor* editor,
                                                   Client* client,
                                                   cz::Str path,
                                                   void* data) {
    WITH_SELECTED_BUFFER(client);
    if (!reset_mode_as_if_named(editor, buffer, handle, path))
        client->show_message("Error: invalid path");
}

REGISTER_COMMAND(command_pretend_rename_buffer);
void command_pretend_rename_buffer(Editor* editor, Command_Source source) {
    bool is_temporary;
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        is_temporary = buffer->type == Buffer::TEMPORARY;
        buffer->render_name(cz::heap_allocator(), &path);
    }

    Dialog dialog = {};
    dialog.prompt = "Pretend rename buffer to: ";
    dialog.completion_engine = is_temporary ? no_completion_engine : file_completion_engine;
    dialog.response_callback = command_pretend_rename_buffer_callback;
    dialog.next_token = syntax::path_next_token;
    dialog.mini_buffer_contents = path;
    source.client->show_dialog(dialog);
}

static void command_delete_file_and_kill_buffer_callback(Editor* editor,
                                                         Client* client,
                                                         cz::Str,
                                                         void* data) {
    Window_Unified* window = client->selected_normal_window;

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_WINDOW_BUFFER(window, client);

        // Prevent killing special buffers (*scratch*, *splash
        // page*, *client messages*, and *client mini buffer*).
        if (buffer->id.value < 4) {
            return;
        }

        if (!buffer->get_path(cz::heap_allocator(), &path)) {
            client->show_message("Couldn't remove temporary file");
            return;
        }
    }

    if (!cz::file::remove_file(path.buffer)) {
        client->show_message("Failed to remove file");
        return;
    }

    // @KillBuffer
    editor->kill(window->buffer_handle.get());

    if (remove_windows_for_buffer(client, window->buffer_handle, editor->buffers[0])) {
        (void)pop_jump(editor, client);
    }
}

REGISTER_COMMAND(command_delete_file_and_kill_buffer);
void command_delete_file_and_kill_buffer(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Confirm deleting file: ";
    dialog.response_callback = command_delete_file_and_kill_buffer_callback;
    source.client->show_dialog(dialog);
}

static void command_diff_buffer_contents_against_callback(Editor* editor,
                                                          Client* client,
                                                          cz::Str path,
                                                          void* data) {
    WITH_CONST_SELECTED_BUFFER(client);

    char temp_file_buffer[L_tmpnam];
    if (!tmpnam(temp_file_buffer)) {
        client->show_message("No temporary file available");
        return;
    }

    if (!save_buffer_to(buffer, temp_file_buffer)) {
        client->show_message("Couldn't save to temporary file");
        return;
    }

    cz::Heap_String name = {};
    CZ_DEFER(name.drop());
    name = cz::format("diff ", path, " ");
    buffer->render_name(cz::heap_allocator(), &name);

    cz::Str args[] = {"diff", path, temp_file_buffer};

    run_console_command(client, editor, buffer->directory.buffer, args, name);
}

static void command_diff_buffer_file_against_callback(Editor* editor,
                                                      Client* client,
                                                      cz::Str input_path,
                                                      void* data) {
    {
        WITH_SELECTED_BUFFER(client);
        (void)save_buffer(buffer);
    }

    WITH_CONST_SELECTED_BUFFER(client);
    cz::String buffer_path = {};
    CZ_DEFER(buffer_path.drop(cz::heap_allocator()));

    buffer_path.reserve(cz::heap_allocator(), buffer->directory.len + buffer->name.len);
    if (buffer->type != Buffer::FILE) {
        client->show_message("Error: buffer must be a file");
        return;
    }
    buffer_path.append(buffer->directory);
    buffer_path.append(buffer->name);

    cz::Heap_String command = {};
    CZ_DEFER(command.drop());
    command = cz::format("diff ", cz::Process::escape_arg(input_path), " ",
                         cz::Process::escape_arg(buffer_path));
    run_console_command(client, editor, buffer->directory.buffer, command.buffer, command);
}

REGISTER_COMMAND(command_diff_buffer_contents_against);
void command_diff_buffer_contents_against(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        path.reserve(cz::heap_allocator(), buffer->directory.len + buffer->name.len);
        path.append(buffer->directory);
        if (buffer->type == Buffer::FILE) {
            path.append(buffer->name);
        }
    }

    Dialog dialog = {};
    dialog.prompt = "Diff buffer against: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_diff_buffer_contents_against_callback;
    dialog.next_token = syntax::path_next_token;
    dialog.mini_buffer_contents = path;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_diff_buffer_file_against);
void command_diff_buffer_file_against(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        path.reserve(cz::heap_allocator(), buffer->directory.len + buffer->name.len);
        if (buffer->type != Buffer::FILE) {
            source.client->show_message("Error: buffer must be a file");
            return;
        }
        path.append(buffer->directory);
        path.append(buffer->name);
    }

    Dialog dialog = {};
    dialog.prompt = "Diff buffer file against: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_diff_buffer_file_against_callback;
    dialog.next_token = syntax::path_next_token;
    dialog.mini_buffer_contents = path;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_reload_buffer);
void command_reload_buffer(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    const char* message = reload_file(buffer);
    if (message)
        source.client->show_message(message);
}

}
}
