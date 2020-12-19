#include "directory_commands.hpp"

#include <cz/fs/directory.hpp>
#include <cz/path.hpp>
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
#include "movement.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace mag {
namespace basic {

void command_directory_reload(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (reload_directory_buffer(buffer).is_err()) {
        source.client->show_message("Couldn't reload directory");
    }
}

static bool get_path(Buffer* buffer, cz::String* path, uint64_t point) {
    Contents_Iterator start = buffer->contents.iterator_at(point);
    Contents_Iterator end = start;
    start_of_line(&start);
    forward_char(&start);
    forward_char(&start);
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
    if (is_directory(path->buffer())) {
        cz::fs::DirectoryIterator iterator(cz::heap_allocator());
        CZ_TRY(iterator.create(path->buffer()));

        while (!iterator.done()) {
            cz::Str file = iterator.file();

            size_t len = path->len();
            path->reserve(cz::heap_allocator(), file.len + 2);
            path->push('/');
            path->append(file);
            path->null_terminate();

            cz::Result result = remove_path(path);

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
        return remove_empty_directory(path->buffer());
    } else {
        return remove_file(path->buffer());
    }
}

void command_directory_delete_path(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    WITH_SELECTED_BUFFER(source.client);
    if (!get_path(buffer, &path, window->cursors[0].point)) {
        source.client->show_message("Cursor not on a valid path");
        return;
    }

    if (remove_path(&path).is_err()) {
        source.client->show_message("Couldn't delete path");
        return;
    }
}

static void command_directory_rename_path_callback(Editor* editor,
                                                   Client* client,
                                                   cz::Str query,
                                                   void*) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    WITH_SELECTED_BUFFER(client);
    if (!get_path(buffer, &path, window->cursors[0].point)) {
        client->show_message("Cursor not on a valid path");
        return;
    }

    cz::String new_path;
    if (cz::path::is_absolute(query)) {
        new_path = query.duplicate_null_terminate(cz::heap_allocator());
    } else {
        new_path.reserve(cz::heap_allocator(), buffer->directory.len() + query.len + 1);
        new_path.append(buffer->directory);
        new_path.append(query);
        new_path.null_terminate();
    }
    CZ_DEFER(new_path.drop(cz::heap_allocator()));

    if (rename(path.buffer(), new_path.buffer()) != 0) {
        client->show_message("Couldn't rename file");
        return;
    }
}

void command_directory_rename_path(Editor* editor, Command_Source source) {
    source.client->show_dialog(editor, "Rename file to: ", file_completion_engine,
                               command_directory_rename_path_callback, nullptr);

    fill_mini_buffer_with_selected_window_directory(editor, source.client);
}

void command_directory_open_path(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    {
        WITH_SELECTED_BUFFER(source.client);
        if (!get_path(buffer, &path, window->cursors[0].point)) {
            return;
        }
    }

    if (path.len() > 0) {
        open_file(editor, source.client, path);
    }
}

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
    if (mkdir(path, 0644) == 0) {
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
        client->show_message("Couldn't create directory");
    } else if (res == 2) {
        client->show_message("Directory already exists");
    }
}

void command_create_directory(Editor* editor, Command_Source source) {
    source.client->show_dialog(editor, "Create directory: ", file_completion_engine,
                               command_create_directory_callback, nullptr);

    fill_mini_buffer_with_selected_window_directory(editor, source.client);
}

}
}
