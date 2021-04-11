#include "directory_commands.hpp"

#include <errno.h>
#include <stdio.h>
#include <cz/file.hpp>
#include <cz/fs/directory.hpp>
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

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace mag {
namespace basic {

static int create_directory(const char* path) {
#ifdef _WIN32
    if (CreateDirectoryA(path, NULL)) {
        return 0;
    }

    int error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS) {
        return 2;
    } else {
        return 1;
    }
#else
    if (mkdir(path, 0755) == 0) {
        return 0;
    }

    int error = errno;
    if (error == EEXIST) {
        return 2;
    } else {
        return 1;
    }
#endif
}

static void get_selected_entry(Window_Unified* window,
                               Buffer* buffer,
                               bool* has_entry,
                               SSOStr* selected) {
    // :DirectorySortFormat
    const size_t offset = 22;

    Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);
    start_of_line(&it);

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

    Contents_Iterator second_line_iterator = buffer->contents.start();
    forward_line(buffer->mode, &second_line_iterator);

    if (has_entry) {
        Contents_Iterator it = second_line_iterator;
        while (it.position + offset < buffer->contents.len) {
            it.advance(offset);
            Contents_Iterator eol = it;
            end_of_line(&eol);
            if (matches(it, eol.position, selected)) {
                second_line_iterator = it;
                break;
            }

            end_of_line(&it);
            forward_char(&it);
        }
    }

    window->cursors[0].point = window->cursors[0].mark = second_line_iterator.position;
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

        cz::fs::DirectoryIterator iterator(cz::heap_allocator());
        CZ_TRY(iterator.create(path->buffer()));

        while (!iterator.done()) {
            cz::Str file = iterator.file();

            size_t len = path->len();
            path->reserve(cz::heap_allocator(), file.len + 2);
            path->push('/');
            path->append(file);
            path->null_terminate();

            cz::Result result = for_each_file(path, file_callback, directory_start_callback,
                                              directory_end_callback);

            path->set_len(len);

            if (result.is_err()) {
                // ignore errors in destruction
                iterator.destroy();
                return result;
            }

            result = iterator.advance();
            if (result.is_err()) {
                // ignore errors in destruction
                iterator.destroy();
                return result;
            }
        }

        CZ_TRY(iterator.destroy());

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
    if (!get_path(buffer, &path, window->cursors[0].point)) {
        client->show_message(editor, "Cursor not on a valid path");
        return;
    }

    if (remove_path(&path).is_err()) {
        client->show_message(editor, "Couldn't delete path");
        return;
    }
}

void command_directory_delete_path(Editor* editor, Command_Source source) {
    source.client->show_dialog(editor, "Confirm delete path: ", no_completion_engine,
                               command_directory_delete_path_callback, nullptr);
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
            int res = create_directory(new_path->buffer());
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
        if (!get_path(buffer, &path, window->cursors[0].point)) {
            client->show_message(editor, "Cursor not on a valid path");
            return;
        }

        if (cz::path::is_absolute(query)) {
            new_path = query.duplicate_null_terminate(cz::heap_allocator());
        } else {
            new_path.reserve(cz::heap_allocator(), buffer->directory.len() + query.len + 1);
            new_path.append(buffer->directory);
            new_path.append(query);
            new_path.null_terminate();
        }
    }

    if (cz::file::is_directory(new_path.buffer())) {
        cz::Option<cz::Str> name = cz::path::name_component(path);
        if (name.is_present) {
            new_path.reserve(cz::heap_allocator(), name.value.len + 1);
            new_path.append(name.value);
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
        if (!get_path(buffer, &path, window->cursors[0].point)) {
            source.client->show_message(editor, "Cursor not on a valid path");
            return;
        }
    }

    source.client->show_dialog(editor, "Copy file to: ", file_completion_engine,
                               command_directory_copy_path_callback, nullptr);

    fill_mini_buffer_with(editor, source.client, path);
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
        if (!get_path(buffer, &path, window->cursors[0].point)) {
            client->show_message(editor, "Cursor not on a valid path");
            return;
        }

        if (cz::path::is_absolute(query)) {
            new_path = query.duplicate_null_terminate(cz::heap_allocator());
        } else {
            new_path.reserve(cz::heap_allocator(), buffer->directory.len() + query.len + 1);
            new_path.append(buffer->directory);
            new_path.append(query);
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
        if (!get_path(buffer, &path, window->cursors[0].point)) {
            source.client->show_message(editor, "Cursor not on a valid path");
            return;
        }
    }

    source.client->show_dialog(editor, "Rename file to: ", file_completion_engine,
                               command_directory_rename_path_callback, nullptr);

    fill_mini_buffer_with(editor, source.client, path);
}

void command_directory_open_path(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!get_path(buffer, &path, window->cursors[0].point)) {
            return;
        }
        push_jump(window, source.client, buffer);
    }

    if (path.len() > 0) {
        open_file(editor, source.client, path);
    }
}

void command_directory_run_path(Editor* editor, Command_Source source) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        directory = buffer->directory.clone_null_terminate(cz::heap_allocator());
        if (!get_path(buffer, &path, window->cursors[0].point)) {
            return;
        }
    }

    if (path.len() > 0) {
        cz::Process_Options options;
        options.working_directory = directory.buffer();

#ifdef _WIN32
        cz::path::convert_to_back_slashes(path.buffer(), path.len());
        cz::Str args[] = {"cmd", "/C", "start", path};
#else
        cz::Str args[] = {path};
#endif

        cz::Process process;
        if (!process.launch_program(args, &options)) {
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
}

static void command_create_directory_callback(Editor* editor,
                                              Client* client,
                                              cz::Str query,
                                              void*) {
    cz::String new_path;
    if (cz::path::is_absolute(query)) {
        new_path = query.duplicate_null_terminate(cz::heap_allocator());
    } else {
        WITH_SELECTED_BUFFER(client);

        new_path.reserve(cz::heap_allocator(), buffer->directory.len() + query.len + 1);
        new_path.append(buffer->directory);
        new_path.append(query);
        new_path.null_terminate();
    }
    CZ_DEFER(new_path.drop(cz::heap_allocator()));

    int res = create_directory(new_path.buffer());
    if (res == 1) {
        client->show_message(editor, "Couldn't create directory");
    } else if (res == 2) {
        client->show_message(editor, "Directory already exists");
    }
}

void command_create_directory(Editor* editor, Command_Source source) {
    source.client->show_dialog(editor, "Create directory: ", file_completion_engine,
                               command_create_directory_callback, nullptr);

    fill_mini_buffer_with_selected_window_directory(editor, source.client);
}

}
}
