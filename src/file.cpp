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

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

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

static bool is_directory(const char* path) {
#ifdef _WIN32
    DWORD result = GetFileAttributes(path);
    if (result == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return result & FILE_ATTRIBUTE_DIRECTORY;
#else
    struct stat buf;
    if (stat(path, &buf) < 0) {
        return false;
    }
    return S_ISDIR(buf.st_mode);
#endif
}

static cz::Result load_directory(Editor* editor,
                                 char* path,
                                 size_t path_len,
                                 Buffer_Id* buffer_id) {
    path[path_len++] = '/';

    Buffer buffer = {};
    buffer.type = Buffer::DIRECTORY;
    buffer.directory = cz::Str(path, path_len).duplicate_null_terminate(cz::heap_allocator());
    buffer.name = cz::Str(".").duplicate(cz::heap_allocator());
    buffer.read_only = true;

    cz::Result result = reload_directory_buffer(&buffer);
    if (result.is_err()) {
        buffer.contents.drop();
        buffer.name.drop(cz::heap_allocator());
        buffer.directory.drop(cz::heap_allocator());
        return result;
    }

    *buffer_id = editor->create_buffer(buffer);
    return result;
}

cz::Result reload_directory_buffer(Buffer* buffer) {
    cz::Buffer_Array buffer_array;
    buffer_array.create();
    CZ_DEFER(buffer_array.drop());

    cz::Vector<cz::String> files = {};
    CZ_DEFER(files.drop(cz::heap_allocator()));

    CZ_TRY(cz::fs::files(cz::heap_allocator(), buffer_array.allocator(), buffer->directory.buffer(),
                         &files));

    std::sort(files.start(), files.end());

    cz::String file = {};
    CZ_DEFER(file.drop(cz::heap_allocator()));
    file.reserve(cz::heap_allocator(), buffer->directory.len());
    file.append(buffer->directory);

    for (size_t i = 0; i < files.len(); ++i) {
        file.set_len(buffer->directory.len());

        file.reserve(cz::heap_allocator(), files[i].len() + 1);
        file.append(files[i]);
        file.null_terminate();

        if (is_directory(file.buffer())) {
            buffer->contents.append("/ ");
        } else {
            buffer->contents.append("  ");
        }

        buffer->contents.append(files[i]);
        buffer->contents.append("\n");
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

    Buffer buffer = {};
    buffer.type = Buffer::FILE;
    const char* end_dir = cz::Str(path, path_len).rfind('/');
    if (end_dir) {
        ++end_dir;
        buffer.directory =
            cz::Str(path, end_dir - path).duplicate_null_terminate(cz::heap_allocator());
        buffer.name = cz::Str(end_dir, path + path_len - end_dir).duplicate(cz::heap_allocator());
    } else {
        buffer.name = cz::Str(path, path_len).duplicate(cz::heap_allocator());
    }

    *buffer_id = editor->create_buffer(buffer);
    path[path_len] = '\0';
    return load_file(editor, path, *buffer_id);
}

bool find_buffer_by_path(Editor* editor, Client* client, cz::Str path, Buffer_Id* buffer_id) {
    if (path.len == 0) {
        return false;
    }

    cz::Str directory;
    cz::Str name;
    const char* ptr = cz::Str(path.buffer, path.len).rfind('/');
    if (ptr) {
        ptr++;
        directory = {path.buffer, size_t(ptr - path.buffer)};
        name = {ptr, size_t(path.end() - ptr)};
    } else {
        directory = {};
        name = path;
    }

    for (size_t i = 0; i < editor->buffers.len(); ++i) {
        Buffer_Handle* handle = editor->buffers[i];

        {
            Buffer* buffer = handle->lock();
            CZ_DEFER(handle->unlock());

            if (buffer->directory == directory && buffer->name == name) {
                goto ret;
            }

            if (buffer->type == Buffer::DIRECTORY) {
                cz::Str d = buffer->directory;
                d.len--;
                if (d == path) {
                    goto ret;
                }
            }

            continue;
        }

    ret:
        *buffer_id = handle->id;
        return true;
    }
    return false;
}

bool find_temp_buffer(Editor* editor, Client* client, cz::Str path, Buffer_Id* buffer_id) {
    cz::Str name;
    cz::Str directory = {};
    const char* ptr = path.find("* (");
    if (ptr) {
        name = {path.buffer, size_t(ptr + 1 - path.buffer)};
        directory = {ptr + 3, size_t(path.end() - (ptr + 3) - 1)};
    } else {
        name = path;
        if (name.ends_with("* ")) {
            name.len--;
        }
    }

    for (size_t i = 0; i < editor->buffers.len(); ++i) {
        Buffer_Handle* handle = editor->buffers[i];

        Buffer* buffer = handle->lock();
        CZ_DEFER(handle->unlock());

        if (buffer->name == name) {
            if (!directory.buffer || buffer->directory == directory) {
                *buffer_id = handle->id;
                return true;
            }
        }
    }

    return false;
}

void open_file(Editor* editor, Client* client, cz::Str user_path) {
    if (user_path.len == 0) {
        client->show_message("File path must not be empty");
        return;
    }

    cz::String fs_user_path = user_path.duplicate(cz::heap_allocator());
    CZ_DEFER(fs_user_path.drop(cz::heap_allocator()));
    cz::path::convert_to_forward_slashes(fs_user_path.buffer(), fs_user_path.len());

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    cz::path::make_absolute(fs_user_path, cz::heap_allocator(), &path);
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

bool save_buffer(Buffer* buffer) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    if (!buffer->get_path(cz::heap_allocator(), &path)) {
        return false;
    }

    if (save_contents(&buffer->contents, path.buffer())) {
        buffer->mark_saved();
        return true;
    }
    return false;
}

void save_contents(const Contents* contents, cz::Output_File file) {
    for (size_t bucket = 0; bucket < contents->buckets.len(); ++bucket) {
        file.write_text(contents->buckets[bucket].elems, contents->buckets[bucket].len);
    }
}

void save_contents_binary(const Contents* contents, cz::Output_File file) {
    for (size_t bucket = 0; bucket < contents->buckets.len(); ++bucket) {
        file.write_binary(contents->buckets[bucket].elems, contents->buckets[bucket].len);
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

bool save_contents_binary(const Contents* contents, const char* path) {
    cz::Output_File file;
    if (!file.open(path)) {
        return false;
    }
    CZ_DEFER(file.close());

    save_contents_binary(contents, file);
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

bool save_contents_to_temp_file_binary(const Contents* contents, cz::Input_File* fd) {
    char temp_file_buffer[L_tmpnam];
    tmpnam(temp_file_buffer);
    if (!save_contents_binary(contents, temp_file_buffer)) {
        return false;
    }
    // Todo: don't open the file twice, instead open it once in read/write mode and reset the head.
    return fd->open(temp_file_buffer);
}

}
