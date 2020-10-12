#include "file.hpp"

#include <algorithm>
#include <cz/defer.hpp>
#include <cz/fs/directory.hpp>
#include <cz/fs/read_to_string.hpp>
#include <cz/path.hpp>
#include <cz/process.hpp>
#include <cz/try.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "config.hpp"
#include "editor.hpp"

namespace mag {

static cz::Result load_file(Editor* editor, const char* path, Buffer_Id buffer_id) {
    FILE* file = fopen(path, "r");
    if (!file) {
        return cz::Result::last_error();
    }

    CZ_DEFER(fclose(file));

    {
        WITH_BUFFER(buffer_id);
        cz::String contents = {};
        CZ_DEFER(contents.drop(cz::heap_allocator()));
        CZ_TRY(cz::fs::read_to_string(cz::heap_allocator(), &contents, file));
        buffer->contents.insert(0, contents);
    }

    return cz::Result::ok();
}

static cz::Result load_directory(Editor* editor,
                                 char* path,
                                 size_t path_len,
                                 Buffer_Id* buffer_id) {
    path[path_len++] = '/';
    path[path_len] = '\0';

    cz::Buffer_Array buffer_array;
    buffer_array.create();
    CZ_DEFER(buffer_array.drop());

    cz::Vector<cz::String> files = {};
    CZ_DEFER(files.drop(cz::heap_allocator()));

    CZ_TRY(cz::fs::files(cz::heap_allocator(), buffer_array.allocator(), path, &files));

    *buffer_id = editor->create_buffer({path, path_len});

    std::sort(files.start(), files.end());

    {
        WITH_BUFFER(*buffer_id);
        for (size_t i = 0; i < files.len(); ++i) {
            buffer->contents.append(files[i]);
            buffer->contents.append("\n");
        }
    }

    return cz::Result::ok();
}

static cz::Result load_path(Editor* editor, char* path, size_t path_len, Buffer_Id* buffer_id) {
    // Try reading it as a directory, then if that fails read it as a file.  On
    // linux, opening it as a file will succeed even if it is a directory.  Then
    // reading the file will cause an error.
    if (load_directory(editor, path, path_len, buffer_id).is_ok()) {
        return cz::Result::ok();
    }

    *buffer_id = editor->create_buffer({path, path_len});
    path[path_len] = '\0';
    return load_file(editor, path, *buffer_id);
}

bool find_buffer_by_path(Editor* editor, Client* client, cz::Str path, Buffer_Id* buffer_id) {
    if (path.len > 0 && path[path.len - 1] == '/') {
        --path.len;
    }

    for (size_t i = 0; i < editor->buffers.len(); ++i) {
        Buffer_Handle* handle = editor->buffers[i];

        {
            Buffer* buffer = handle->lock();
            CZ_DEFER(handle->unlock());
            cz::Str buffer_path = buffer->path;

            if (buffer_path.len > 0 && buffer_path[buffer_path.len - 1] == '/') {
                --buffer_path.len;
            }
            if (buffer_path != path) {
                continue;
            }
        }

        *buffer_id = handle->id;
        return true;
    }
    return false;
}

void open_file(Editor* editor, Client* client, cz::Str user_path) {
    if (user_path.len == 0) {
        client->show_message("File path must not be empty");
        return;
    }

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    cz::path::make_absolute(user_path, cz::heap_allocator(), &path);
    if (path[path.len() - 1] == '/') {
        path.pop();
    }
    path.reserve(cz::heap_allocator(), 2);

    Buffer_Id buffer_id;
    if (!find_buffer_by_path(editor, client, path, &buffer_id)) {
        if (load_path(editor, path.buffer(), path.len(), &buffer_id).is_err()) {
            client->show_message("File not found");
            // Still open empty file buffer.
        }
    }

    client->set_selected_buffer(buffer_id);
}

static void save_contents(const Contents* contents, cz::Output_File file) {
    for (size_t bucket = 0; bucket < contents->buckets.len(); ++bucket) {
        file.write_text(contents->buckets[bucket].elems, contents->buckets[bucket].len);
    }
}

bool save_contents(const Contents* contents, const char* path) {
    cz::Output_File file;
    if (!file.open(path)) {
        return false;
    }
    CZ_DEFER(file.close());

    save_contents(contents, file);
    return true;
}

bool save_contents_to_temp_file(const Contents* contents, cz::Input_File* fd) {
    char temp_file_buffer[L_tmpnam];
    tmpnam(temp_file_buffer);
    if (!save_contents(contents, temp_file_buffer)) {
        return false;
    }
    // Todo: don't open the file twice, instead open it once in read/write mode and reset the head.
    return fd->open(temp_file_buffer);
}

}
