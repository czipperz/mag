#include "file.hpp"

#include <algorithm>
#include <cz/defer.hpp>
#include <cz/fs/directory.hpp>
#include <cz/fs/read_to_string.hpp>
#include <cz/path.hpp>
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

    cz::BufferArray buffer_array;
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
            buffer->contents.insert(buffer->contents.len, files[i]);
            buffer->contents.insert(buffer->contents.len, "\n");
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
        Message message = {};
        message.tag = Message::SHOW;
        message.text = "File path must not be empty";
        client->show_message(message);
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
            Message message = {};
            message.tag = Message::SHOW;
            message.text = "File not found";
            client->show_message(message);
            // Still open empty file buffer.
        }
    }

    client->set_selected_buffer(buffer_id);
}

bool save_contents(const Contents* contents, const char* path) {
    FILE* file = fopen(path, "w");
    if (!file) {
        return false;
    }
    CZ_DEFER(fclose(file));

    for (size_t bucket = 0; bucket < contents->buckets.len(); ++bucket) {
        fwrite(contents->buckets[bucket].elems, 1, contents->buckets[bucket].len, file);
    }

    return true;
}

}
