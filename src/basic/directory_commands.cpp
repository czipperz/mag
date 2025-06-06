#include "directory_commands.hpp"

#include <errno.h>
#include <stdio.h>
#include <algorithm>
#include <cz/directory.hpp>
#include <cz/file.hpp>
#include <cz/format.hpp>
#include <cz/heap_string.hpp>
#include <cz/path.hpp>
#include <cz/process.hpp>
#include "buffer_commands.hpp"
#include "core/client.hpp"
#include "core/command.hpp"
#include "core/command_macros.hpp"
#include "core/completion.hpp"
#include "core/contents.hpp"
#include "core/editor.hpp"
#include "core/file.hpp"
#include "core/job.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "syntax/tokenize_path.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace mag {
namespace basic {

static void get_selected_entry(Window_Unified* window,
                               Buffer* buffer,
                               bool* has_entry,
                               SSOStr* selected) {
    // :DirectorySortFormat
    const size_t offset = 22;

    Contents_Iterator it =
        buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
    start_of_line(&it);

    // The first line is the title bar.
    if (it.position == 0) {
        *has_entry = false;
        return;
    }

    *has_entry = it.position + offset < buffer->contents.len;
    if (*has_entry) {
        it.advance(offset);
        Contents_Iterator end = it;
        end_of_line(&end);
        *selected = buffer->contents.slice(cz::heap_allocator(), it, end.position);
    }
}

static void reload_directory_window(Editor* editor,
                                    Client* client,
                                    Window_Unified* window,
                                    Buffer* buffer,
                                    bool has_entry,
                                    cz::Str selected) {
    // :DirectorySortFormat
    const size_t offset = 22;

    if (!reload_directory_buffer(buffer)) {
        client->show_message("Couldn't reload directory");
        return;
    }

    kill_extra_cursors(window, client);

    uint64_t cursor_position = 0;

    if (has_entry) {
        Contents_Iterator it = buffer->contents.start();
        while (it.position + offset < buffer->contents.len) {
            it.advance(offset);
            Contents_Iterator eol = it;
            end_of_line(&eol);
            if (matches(it, eol.position, selected)) {
                cursor_position = it.position;
                break;
            }

            end_of_line(&it);
            forward_char(&it);
        }
    } else {
        // There are 26 columns on the first line in the shortest configuration.
        cursor_position = std::min(window->cursors[0].point, (uint64_t)26);
    }

    window->cursors[0].point = window->cursors[0].mark = cursor_position;
}

REGISTER_COMMAND(command_directory_reload);
void command_directory_reload(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    bool has_entry;
    SSOStr selected;
    get_selected_entry(window, buffer, &has_entry, &selected);
    CZ_DEFER(if (has_entry) { selected.drop(cz::heap_allocator()); });

    reload_directory_window(editor, source.client, window, buffer, has_entry, selected.as_str());
}

REGISTER_COMMAND(command_directory_toggle_sort);
void command_directory_toggle_sort(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    bool has_entry;
    SSOStr selected;
    get_selected_entry(window, buffer, &has_entry, &selected);
    CZ_DEFER(if (has_entry) { selected.drop(cz::heap_allocator()); });

    // :DirectorySortFormat
    bool sort_names = buffer->contents.iterator_at(19).get() != 'V';
    sort_names = !sort_names;
    if (sort_names) {
        buffer->contents.remove(18, 3);
        buffer->contents.insert(18, "   ");
        buffer->contents.insert(26, " (V)");
    } else {
        buffer->contents.remove(18, 3);
        buffer->contents.insert(18, "(V)");
        buffer->contents.remove(26, 4);
    }

    reload_directory_window(editor, source.client, window, buffer, has_entry, selected.as_str());
}

static bool get_path(const Buffer* buffer, cz::String* path, uint64_t point) {
    Contents_Iterator start = buffer->contents.iterator_at(point);
    Contents_Iterator end = start;
    start_of_line(&start);
    if (start.at_bob()) {
        return false;
    }
    start.advance_most(22);
    end_of_line(&end);

    if (start.position >= end.position) {
        return false;
    }

    path->reserve(cz::heap_allocator(), buffer->directory.len + end.position - start.position + 1);
    path->append(buffer->directory);
    buffer->contents.slice_into(start, end.position, path);
    path->null_terminate();
    return true;
}

template <class File_Callback, class Directory_Start_Callback, class Directory_End_Callback>
static bool for_each_file(cz::String* path,
                          File_Callback file_callback,
                          Directory_Start_Callback directory_start_callback,
                          Directory_End_Callback directory_end_callback) {
    if (cz::file::is_directory(path->buffer)) {
        if (!directory_start_callback(path->buffer))
            return false;

        cz::Heap_String file = {};
        CZ_DEFER(file.drop());

        cz::Directory_Iterator iterator;
        int result = iterator.init(path->buffer);
        while (result > 0) {
            cz::Str file = iterator.str_name();

            size_t len = path->len;
            path->reserve(cz::heap_allocator(), file.len + 2);
            path->push('/');
            path->append(file);
            path->null_terminate();

            bool success = for_each_file(path, file_callback, directory_start_callback,
                                         directory_end_callback);

            path->len = len;

            if (!success) {
                // ignore errors in destruction
                iterator.drop();
                return success;
            }

            result = iterator.advance();
            if (result <= 0) {
                if (!iterator.drop())
                    result = -1;
                break;
            }
        }

        path->null_terminate();
        if (!directory_end_callback(path->buffer))
            return false;
        return result >= 0;
    } else {
        return file_callback(path->buffer);
    }
}

static bool remove_path(cz::String* path) {
    return for_each_file(
        path, cz::file::remove_file, [](const char*) { return true; },
        cz::file::remove_empty_directory);
}

static void command_directory_delete_path_callback(Editor* editor, Client* client, cz::Str, void*) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    WITH_SELECTED_BUFFER(client);
    if (!get_path(buffer, &path, window->cursors[window->selected_cursor].point)) {
        client->show_message("Cursor not on a valid path");
        return;
    }

    if (!remove_path(&path)) {
        client->show_message("Couldn't delete path");
        return;
    }
}

REGISTER_COMMAND(command_directory_delete_path);
void command_directory_delete_path(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Submit to confirm delete path: ";
    dialog.response_callback = command_directory_delete_path_callback;
    source.client->show_dialog(dialog);
}

static bool copy_path(cz::String* path, cz::String* new_path) {
    size_t base = path->len;

    size_t new_path_len = new_path->len;
    auto set_new_path = [&]() {
        new_path->len = new_path_len;

        cz::Str extra = path->as_str().slice_start(base);
        new_path->reserve(cz::heap_allocator(), extra.len + 1);
        new_path->append(extra);
        new_path->null_terminate();
    };

    return for_each_file(
        path,
        [&](const char* src) {
            // file
            set_new_path();

            cz::Input_File input;
            CZ_DEFER(input.close());
            if (!input.open(src)) {
                return false;
            }

            cz::Output_File output;
            CZ_DEFER(output.close());
            if (!output.open(new_path->buffer)) {
                return false;
            }

            char buffer[1024];
            while (1) {
                int64_t read = input.read(buffer, sizeof(buffer));
                if (read > 0) {
                    int64_t wrote = output.write(buffer, read);
                    if (wrote != read) {
                        return false;
                    }
                } else if (read == 0) {
                    return true;
                } else {
                    return false;
                }
            }
        },
        [&](const char* src) {
            // directory
            set_new_path();
            int res = cz::file::create_directory(new_path->buffer);
            return res == 0;
        },
        [](const char*) { return true; });
}

static bool make_parent_directories(cz::Str path) {
    cz::Str directory = path;
    if (!cz::path::pop_component(&directory))
        return true;

    cz::Heap_String storage = {};
    CZ_DEFER(storage.drop());

    while (1) {
        cz::Str base = directory;
        cz::Str prev = directory;

        // Loop popping directories until we reach the first directory that exists.
        while (1) {
            storage.len = 0;
            storage.reserve_exact(base.len + 1);
            storage.append(base);
            storage.null_terminate();
            if (cz::file::exists(storage.buffer))
                break;

            prev = base;
            if (!cz::path::pop_component(&base)) {
                // Root directory doesn't exist.  Fail.
                return false;
            }
        }

        if (base.len == directory.len)
            break;

        storage.len = 0;
        storage.reserve_exact(prev.len + 1);
        storage.append(prev);
        storage.null_terminate();
        if (cz::file::create_directory(storage.buffer) != 0)
            return false;
    }
    return true;
}

static void command_directory_copy_path_callback(Editor* editor,
                                                 Client* client,
                                                 cz::Str query,
                                                 void*) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    cz::String new_path = {};
    CZ_DEFER(new_path.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(client);
        if (!get_path(buffer, &path, window->cursors[window->selected_cursor].point)) {
            client->show_message("Cursor not on a valid path");
            return;
        }

        new_path = standardize_path(cz::heap_allocator(), query);
    }

    if (cz::file::is_directory(new_path.buffer) ||
        (!cz::file::is_directory(path.buffer) && query.ends_with('/'))) {
        cz::Str name;
        if (cz::path::name_component(path, &name)) {
            new_path.reserve(cz::heap_allocator(), name.len + 2);
            if (!new_path.ends_with('/')) {
                new_path.push('/');
            }
            new_path.append(name);
            new_path.null_terminate();
        }
    }

    if (!make_parent_directories(new_path)) {
        client->show_message("Failed to make parent directories for destination");
        return;
    }

    if (!copy_path(&path, &new_path)) {
        client->show_message("Couldn't copy path");
        return;
    }
}

REGISTER_COMMAND(command_directory_copy_path_complete_path);
void command_directory_copy_path_complete_path(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!get_path(buffer, &path, window->cursors[window->selected_cursor].point)) {
            source.client->show_message("Cursor not on a valid path");
            return;
        }
    }

    Dialog dialog = {};
    dialog.prompt = "Copy file to: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_directory_copy_path_callback;
    dialog.mini_buffer_contents = path;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_directory_copy_path_complete_directory);
void command_directory_copy_path_complete_directory(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        path = buffer->directory.clone(cz::heap_allocator());
    }

    Dialog dialog = {};
    dialog.prompt = "Copy file to: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_directory_copy_path_callback;
    dialog.mini_buffer_contents = path;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(dialog);
}

static void command_directory_rename_path_callback(Editor* editor,
                                                   Client* client,
                                                   cz::Str query,
                                                   void*) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    cz::String new_path = {};
    CZ_DEFER(new_path.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(client);
        if (!get_path(buffer, &path, window->cursors[window->selected_cursor].point)) {
            client->show_message("Cursor not on a valid path");
            return;
        }

        new_path = standardize_path(cz::heap_allocator(), query);
    }

    if (cz::file::is_directory(new_path.buffer) ||
        (!cz::file::is_directory(path.buffer) && query.ends_with('/'))) {
        cz::Str name;
        if (cz::path::name_component(path, &name)) {
            new_path.reserve(cz::heap_allocator(), name.len + 2);
            if (!new_path.ends_with('/')) {
                new_path.push('/');
            }
            new_path.append(name);
            new_path.null_terminate();
        }
    }
    if (cz::file::is_directory(new_path.buffer)) {
        client->show_message_format("Cannot overwrite directory ", new_path);
        return;
    }

    if (!make_parent_directories(new_path)) {
        client->show_message("Failed to make parent directories for destination");
        return;
    }

    if (rename(path.buffer, new_path.buffer) != 0) {
        if (!remove_path(&new_path)) {
            client->show_message("Couldn't remove destination");
            return;
        }
        if (rename(path.buffer, new_path.buffer) != 0) {
            client->show_message("Couldn't rename path");
            return;
        }
    }

    cz::Arc<Buffer_Handle> handle = {};
    if (find_buffer_by_path(editor, path, &handle)) {
        WITH_BUFFER_HANDLE(handle);

        cz::Str directory = new_path;
        (void)cz::path::pop_name(new_path, &directory);

        cz::Str name = ".";
        (void)cz::path::name_component(new_path, &name);

        buffer->directory.len = 0;
        buffer->directory.reserve(cz::heap_allocator(), directory.len + 1);
        buffer->directory.append(directory);
        buffer->directory.null_terminate();

        buffer->name.len = 0;
        buffer->name.reserve(cz::heap_allocator(), name.len);
        buffer->name.append(name);
    }
}

REGISTER_COMMAND(command_directory_rename_path_complete_path);
void command_directory_rename_path_complete_path(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!get_path(buffer, &path, window->cursors[window->selected_cursor].point)) {
            source.client->show_message("Cursor not on a valid path");
            return;
        }
    }

    Dialog dialog = {};
    dialog.prompt = "Rename file to: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_directory_rename_path_callback;
    dialog.mini_buffer_contents = path;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_directory_rename_path_complete_directory);
void command_directory_rename_path_complete_directory(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        path = buffer->directory.clone(cz::heap_allocator());
    }

    Dialog dialog = {};
    dialog.prompt = "Rename file to: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_directory_rename_path_callback;
    dialog.mini_buffer_contents = path;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_directory_open_path);
void command_directory_open_path(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!get_path(buffer, &path, window->cursors[window->selected_cursor].point)) {
            return;
        }
        push_jump(window, source.client, buffer);
    }

    if (path.len > 0) {
        open_file(editor, source.client, path);
    }
}

const char* terminal_script = "xterm";

REGISTER_COMMAND(command_directory_run_path);
void command_directory_run_path(Editor* editor, Command_Source source) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    bool got_path;
    {
        WITH_CONST_SELECTED_BUFFER(source.client);

        // If there is no buffer then leave it null so we launch in the current working directory.
        if (buffer->directory.len > 0) {
            directory = buffer->directory.clone_null_terminate(cz::heap_allocator());
        }

        got_path = get_path(buffer, &path, window->cursors[window->selected_cursor].point);
    }

    // As a backup (or if we're on the first line) then launch a terminal instead.
    if (!got_path) {
        return launch_terminal_in(editor, source.client, directory.buffer);
    }

    if (path.len == 0) {
        return;
    }

    cz::Process_Options options;
    options.working_directory = directory.buffer;

    cz::Process process;
    bool success;

    // Run the program that the cursor is on.
#ifdef _WIN32
    cz::path::convert_to_back_slashes(&path);
    cz::Str args[] = {"cmd", "/C", "start", path};
    success = process.launch_program(args, options);
#else
    cz::Str run_program[] = {path};
    success = process.launch_program(run_program, options);
#endif

    if (!success) {
        source.client->show_message_format("Failed to run path ", path);
        return;
    }

    editor->add_asynchronous_job(job_process_silent(process));
}

REGISTER_COMMAND(command_launch_terminal);
void command_launch_terminal(Editor* editor, Command_Source source) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(source.client);

        // If there is no buffer then leave it null so we launch in the current working directory.
        if (buffer->directory.len > 0) {
            directory = buffer->directory.clone_null_terminate(cz::heap_allocator());
        }
    }

    launch_terminal_in(editor, source.client, directory.buffer);
}

void launch_terminal_in(Editor* editor, Client* client, const char* directory) {
    cz::Process_Options options;
    options.working_directory = directory;
    options.detach = true;

    cz::Process process;
    if (!process.launch_script(terminal_script, options)) {
        client->show_message_format("Failed to start terminal ", terminal_script, " in directory ",
                                    directory);
        return;
    }

    editor->add_asynchronous_job(job_process_silent(process));
}

static void command_create_directory_callback(Editor* editor,
                                              Client* client,
                                              cz::Str query,
                                              void*) {
    cz::String new_path = {};
    CZ_DEFER(new_path.drop(cz::heap_allocator()));

    if (cz::path::is_absolute(query)) {
        new_path = query.clone_null_terminate(cz::heap_allocator());
    } else {
        WITH_SELECTED_BUFFER(client);

        new_path.reserve(cz::heap_allocator(), buffer->directory.len + query.len + 1);
        new_path.append(buffer->directory);
        new_path.append(query);
        new_path.null_terminate();
    }

    int res = cz::file::create_directory(new_path.buffer);
    if (res == 1) {
        client->show_message("Couldn't create directory");
    } else if (res == 2) {
        client->show_message("Directory already exists");
    }
}

REGISTER_COMMAND(command_create_directory);
void command_create_directory(Editor* editor, Command_Source source) {
    cz::String selected_window_directory = {};
    CZ_DEFER(selected_window_directory.drop(cz::heap_allocator()));
    get_selected_window_directory(editor, source.client, cz::heap_allocator(),
                                  &selected_window_directory);

    Dialog dialog = {};
    dialog.prompt = "Create directory: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_create_directory_callback;
    dialog.mini_buffer_contents = selected_window_directory;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(dialog);
}

}
}
