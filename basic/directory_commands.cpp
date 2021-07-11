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
#include <cz/result.hpp>
#include <cz/try.hpp>
#include "buffer_commands.hpp"
#include "client.hpp"
#include "command.hpp"
#include "command_macros.hpp"
#include "completion.hpp"
#include "contents.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "job.hpp"
#include "match.hpp"
#include "movement.hpp"
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

    if (reload_directory_buffer(buffer).is_err()) {
        client->show_message(editor, "Couldn't reload directory");
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

void command_directory_reload(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    bool has_entry;
    SSOStr selected;
    get_selected_entry(window, buffer, &has_entry, &selected);
    CZ_DEFER(if (has_entry) { selected.drop(cz::heap_allocator()); });

    reload_directory_window(editor, source.client, window, buffer, has_entry, selected.as_str());
}

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
    for (int i = 0; i < 22; ++i) {
        forward_char(&start);
    }
    end_of_line(&end);

    if (start.position >= end.position) {
        return false;
    }

    path->reserve(cz::heap_allocator(),
                  buffer->directory.len() + end.position - start.position + 1);
    path->append(buffer->directory);
    buffer->contents.slice_into(start, end.position, path);
    path->null_terminate();
    return true;
}

template <class File_Callback, class Directory_Start_Callback, class Directory_End_Callback>
static cz::Result for_each_file(cz::String* path,
                                File_Callback file_callback,
                                Directory_Start_Callback directory_start_callback,
                                Directory_End_Callback directory_end_callback) {
    if (cz::file::is_directory(path->buffer())) {
        CZ_TRY(directory_start_callback(path->buffer()));

        cz::Heap_String file = {};
        CZ_DEFER(file.drop());

        cz::Directory_Iterator iterator;
        CZ_TRY(iterator.init(path->buffer(), cz::heap_allocator(), &file));

        while (!iterator.done()) {
            size_t len = path->len();
            path->reserve(cz::heap_allocator(), file.len() + 2);
            path->push('/');
            path->append(file);
            path->null_terminate();

            cz::Result result = for_each_file(path, file_callback, directory_start_callback,
                                              directory_end_callback);

            path->set_len(len);

            if (result.is_err()) {
                // ignore errors in destruction
                iterator.drop();
                return result;
            }

            result = iterator.advance(cz::heap_allocator(), &file);
            if (result.is_err()) {
                // ignore errors in destruction
                iterator.drop();
                return result;
            }
        }

        CZ_TRY(iterator.drop());

        path->null_terminate();
        return directory_end_callback(path->buffer());
    } else {
        return file_callback(path->buffer());
    }
}

static cz::Result remove_empty_directory(const char* path) {
#ifdef _WIN32
    if (!RemoveDirectoryA(path)) {
        cz::Result result;
        result.code = GetLastError();
        return result;
    }
    return cz::Result::ok();
#else
    if (rmdir(path) != 0) {
        return cz::Result::last_error();
    }
    return cz::Result::ok();
#endif
}

static cz::Result remove_file(const char* path) {
#ifdef _WIN32
    if (!DeleteFileA(path)) {
        cz::Result result;
        result.code = GetLastError();
        return result;
    }
    return cz::Result::ok();
#else
    if (unlink(path) != 0) {
        return cz::Result::last_error();
    }
    return cz::Result::ok();
#endif
}

static cz::Result remove_path(cz::String* path) {
    return for_each_file(
        path, remove_file, [](const char*) { return cz::Result::ok(); }, remove_empty_directory);
}

static void command_directory_delete_path_callback(Editor* editor, Client* client, cz::Str, void*) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    WITH_SELECTED_BUFFER(client);
    if (!get_path(buffer, &path, window->cursors[window->selected_cursor].point)) {
        client->show_message(editor, "Cursor not on a valid path");
        return;
    }

    if (remove_path(&path).is_err()) {
        client->show_message(editor, "Couldn't delete path");
        return;
    }
}

void command_directory_delete_path(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Submit to confirm delete path: ";
    dialog.response_callback = command_directory_delete_path_callback;
    source.client->show_dialog(editor, dialog);
}

static cz::Result copy_path(cz::String* path, cz::String* new_path) {
    size_t base = path->len();

    size_t new_path_len = new_path->len();
    auto set_new_path = [&]() {
        new_path->set_len(new_path_len);

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
                cz::Result result;
                result.code = 1;
                return result;
            }

            cz::Output_File output;
            CZ_DEFER(output.close());
            if (!output.open(new_path->buffer())) {
                cz::Result result;
                result.code = 1;
                return result;
            }

            char buffer[1024];
            while (1) {
                int64_t read = input.read_binary(buffer, sizeof(buffer));
                if (read > 0) {
                    int64_t wrote = output.write_binary(buffer, read);
                    if (wrote < 0) {
                        cz::Result result;
                        result.code = 1;
                        return result;
                    }
                } else if (read == 0) {
                    return cz::Result::ok();
                } else {
                    cz::Result result;
                    result.code = 1;
                    return result;
                }
            }
        },
        [&](const char* src) {
            // directory
            set_new_path();
            int res = cz::file::create_directory(new_path->buffer());
            if (res != 0) {
                cz::Result result;
                result.code = res;
                return result;
            }
            return cz::Result::ok();
        },
        [](const char*) { return cz::Result::ok(); });
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
            client->show_message(editor, "Cursor not on a valid path");
            return;
        }

        new_path = standardize_path(cz::heap_allocator(), query);
    }

    if (cz::file::is_directory(new_path.buffer())) {
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

    if (copy_path(&path, &new_path).is_err()) {
        client->show_message(editor, "Couldn't copy path");
        return;
    }
}

void command_directory_copy_path(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!get_path(buffer, &path, window->cursors[window->selected_cursor].point)) {
            source.client->show_message(editor, "Cursor not on a valid path");
            return;
        }
    }

    Dialog dialog = {};
    dialog.prompt = "Copy file to: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_directory_copy_path_callback;
    dialog.mini_buffer_contents = path;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(editor, dialog);
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
            client->show_message(editor, "Cursor not on a valid path");
            return;
        }

        new_path = standardize_path(cz::heap_allocator(), query);
    }

    if (cz::file::is_directory(new_path.buffer())) {
        cz::Str name;
        if (cz::path::name_component(path, &name)) {
            new_path.reserve(cz::heap_allocator(), name.len + 2);
            if (!new_path.ends_with("/")) {
                new_path.push('/');
            }
            new_path.append(name);
            new_path.null_terminate();
        }
    }

    if (rename(path.buffer(), new_path.buffer()) != 0) {
        client->show_message(editor, "Couldn't rename path");
        return;
    }
}

void command_directory_rename_path(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!get_path(buffer, &path, window->cursors[window->selected_cursor].point)) {
            source.client->show_message(editor, "Cursor not on a valid path");
            return;
        }
    }

    Dialog dialog = {};
    dialog.prompt = "Rename file to: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_directory_rename_path_callback;
    dialog.mini_buffer_contents = path;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(editor, dialog);
}

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

    if (path.len() > 0) {
        open_file(editor, source.client, path);
    }
}

const char* terminal_script = "xterm";

void command_directory_run_path(Editor* editor, Command_Source source) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    bool got_path;
    {
        WITH_CONST_SELECTED_BUFFER(source.client);

        // If there is no buffer then leave it null so we launch in the current working directory.
        if (buffer->directory.len() > 0) {
            directory = buffer->directory.clone_null_terminate(cz::heap_allocator());
        }

        got_path = get_path(buffer, &path, window->cursors[window->selected_cursor].point);
    }

    // As a backup (or if we're on the first line) then launch a terminal instead.
    if (!got_path) {
        return launch_terminal_in(editor, source.client, directory.buffer());
    }

    if (path.len() == 0) {
        return;
    }

    cz::Process_Options options;
    options.working_directory = directory.buffer();

    cz::Process process;
    bool success;

    // Run the program that the cursor is on.
#ifdef _WIN32
    cz::path::convert_to_back_slashes(&path);
    cz::Str args[] = {"cmd", "/C", "start", path};
    success = process.launch_program(args, &options);
#else
    cz::Str run_program[] = {path};
    success = process.launch_program(run_program, &options);
#endif

    if (!success) {
        cz::String string = {};
        CZ_DEFER(string.drop(cz::heap_allocator()));
        cz::Str prefix = "Failed to run path ";
        string.reserve(cz::heap_allocator(), prefix.len + path.len());
        string.append(prefix);
        string.append(path);
        source.client->show_message(editor, string);
        return;
    }

    editor->add_asynchronous_job(job_process_silent(process));
}

void command_launch_terminal(Editor* editor, Command_Source source) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(source.client);

        // If there is no buffer then leave it null so we launch in the current working directory.
        if (buffer->directory.len() > 0) {
            directory = buffer->directory.clone_null_terminate(cz::heap_allocator());
        }
    }

    launch_terminal_in(editor, source.client, directory.buffer());
}

void launch_terminal_in(Editor* editor, Client* client, const char* directory) {
    cz::Process_Options options;
    options.working_directory = directory;

    cz::Process process;
    if (!process.launch_script(terminal_script, &options)) {
        cz::String string = {};
        CZ_DEFER(string.drop(cz::heap_allocator()));
        cz::Str prefix = "Failed to start terminal ";
        cz::Str terminal_script_str = terminal_script;
        cz::Str infix = " in directory ";
        cz::Str directory_str = directory;
        string.reserve(cz::heap_allocator(),
                       prefix.len + terminal_script_str.len + infix.len + directory_str.len);
        string.append(prefix);
        string.append(terminal_script_str);
        string.append(infix);
        string.append(directory_str);
        client->show_message(editor, string);
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

        new_path.reserve(cz::heap_allocator(), buffer->directory.len() + query.len + 1);
        new_path.append(buffer->directory);
        new_path.append(query);
        new_path.null_terminate();
    }

    int res = cz::file::create_directory(new_path.buffer());
    if (res == 1) {
        client->show_message(editor, "Couldn't create directory");
    } else if (res == 2) {
        client->show_message(editor, "Directory already exists");
    }
}

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
    source.client->show_dialog(editor, dialog);
}

}
}
