#define __STDC_WANT_LIB_EXT1__ 1
#include "file.hpp"

#include <time.h>
#include <algorithm>
#include <cz/allocator.hpp>
#include <cz/bit_array.hpp>
#include <cz/defer.hpp>
#include <cz/file.hpp>
#include <cz/fs/directory.hpp>
#include <cz/fs/read_to_string.hpp>
#include <cz/path.hpp>
#include <cz/process.hpp>
#include <cz/sort.hpp>
#include <cz/string.hpp>
#include <cz/try.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "config.hpp"
#include "editor.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace mag {

bool check_out_of_date_and_update_file_time(const char* path, cz::File_Time* file_time) {
    cz::File_Time new_ft;

    if (!cz::get_file_time(path, &new_ft)) {
        return false;
    }

    if (cz::is_file_time_before(*file_time, new_ft)) {
        *file_time = new_ft;
        return true;
    }

    return false;
}

static cz::Result load_file(Buffer* buffer, const char* path) {
    cz::Input_File file;
    if (!file.open(path)) {
        return {1};
    }
    CZ_DEFER(file.close());

    // If we're on Windows, set the default to use carriage returns.
#ifdef _WIN32
    buffer->use_carriage_returns = true;
#else
    buffer->use_carriage_returns = false;
#endif

    cz::Carriage_Return_Carry carry;
    char buf[1024];
    while (1) {
        int64_t res = file.read_binary(buf, sizeof(buf));
        if (res > 0) {
            cz::Str str = {buf, (size_t)res};
            const char* newline = str.find('\n');
            if (newline) {
                // Once we read one line, we can determine if this file in particular should use
                // carriage returns.
                buffer->use_carriage_returns = newline != buf && newline[-1] == '\r';

                cz::strip_carriage_returns(buf, &str.len);
                if (str.buffer[str.len - 1] == '\r') {
                    carry.carrying = true;
                    --str.len;
                }
                buffer->contents.append(str);
                goto read_continue;
            }
            buffer->contents.append(str);
        } else if (res == 0) {
            return cz::Result::ok();
        } else {
            return {1};
        }
    }

read_continue:
    while (1) {
        int64_t res = file.read_strip_carriage_returns(buf, sizeof(buf), &carry);
        if (res > 0) {
            buffer->contents.append({buf, (size_t)res});
        } else if (res == 0) {
            return cz::Result::ok();
        } else {
            return {1};
        }
    }
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

static void sort_files(cz::Slice<cz::Str> files,
                       cz::File_Time* file_times,
                       unsigned char* file_directories,
                       bool sort_names) {
    auto swap = [&](size_t i, size_t j) {
        if (i == j) {
            return;
        }

        cz::Str tfile = files[i];
        cz::File_Time tfile_time = file_times[i];
        bool tfile_directory = cz::bit_array::get(file_directories, i);

        files[i] = files[j];
        file_times[i] = file_times[j];
        if (cz::bit_array::get(file_directories, j)) {
            cz::bit_array::set(file_directories, i);
        } else {
            cz::bit_array::unset(file_directories, i);
        }

        files[j] = tfile;
        file_times[j] = tfile_time;
        if (tfile_directory) {
            cz::bit_array::set(file_directories, j);
        } else {
            cz::bit_array::unset(file_directories, j);
        }
    };

    if (sort_names) {
        cz::generic_sort((size_t)0, files.len,
                         [&](size_t left, size_t right) { return files[left] < files[right]; },
                         swap);
    } else {
        cz::generic_sort((size_t)0, files.len,
                         [&](size_t left, size_t right) {
                             return is_file_time_before(file_times[right], file_times[left]);
                         },
                         swap);
    }
}

cz::Result reload_directory_buffer(Buffer* buffer) {
    cz::Buffer_Array buffer_array;
    buffer_array.create();
    CZ_DEFER(buffer_array.drop());

    cz::Vector<cz::Str> files = {};
    CZ_DEFER(files.drop(cz::heap_allocator()));

    CZ_TRY(cz::fs::files(cz::heap_allocator(), buffer_array.allocator(), buffer->directory.buffer(),
                         &files));

    cz::File_Time* file_times = (cz::File_Time*)malloc(sizeof(cz::File_Time) * files.len());
    CZ_DEFER(free(file_times));

    unsigned char* file_directories =
        (unsigned char*)calloc(1, cz::bit_array::alloc_size(files.len()));
    CZ_DEFER(free(file_directories));

    cz::String file = {};
    CZ_DEFER(file.drop(cz::heap_allocator()));
    file.reserve(cz::heap_allocator(), buffer->directory.len());
    file.append(buffer->directory);

    for (size_t i = 0; i < files.len(); ++i) {
        file.set_len(buffer->directory.len());
        file.reserve(cz::heap_allocator(), files[i].len + 1);
        file.append(files[i]);
        file.null_terminate();

        file_times[i] = {};
        cz::get_file_time(file.buffer(), &file_times[i]);

        if (cz::file::is_directory(file.buffer())) {
            cz::bit_array::set(file_directories, i);
        }
    }

    // :DirectorySortFormat
    bool sort_names = buffer->contents.len == 0 || buffer->contents.iterator_at(19).get() != 'V';

    sort_files(files, file_times, file_directories, sort_names);

    buffer->token_cache.reset();
    buffer->contents.remove(0, buffer->contents.len);

    // :DirectorySortFormat The format of (V) is relied upon by other uses of this tag.
    if (sort_names) {
        buffer->contents.append("Modification Date     File (V)\n");
    } else {
        buffer->contents.append("Modification Date (V) File\n");
    }

    for (size_t i = 0; i < files.len(); ++i) {
        cz::Date date;
        if (cz::file_time_to_date_local(file_times[i], &date)) {
            char date_string[32];
            snprintf(date_string, sizeof(date_string), "%04d/%02d/%02d %02d:%02d:%02d ", date.year,
                     date.month, date.day_of_month, date.hour, date.minute, date.second);
            buffer->contents.append(date_string);
        } else {
            buffer->contents.append("                    ");
        }

        if (cz::bit_array::get(file_directories, i)) {
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

    path[path_len] = '\0';
    cz::Result result = load_file(&buffer, path);
    *buffer_id = editor->create_buffer(buffer);
    return result;
}

cz::String standardize_path(cz::Allocator allocator, cz::Str user_path) {
    cz::String user_path_nt = user_path.duplicate_null_terminate(cz::heap_allocator());
    CZ_DEFER(user_path_nt.drop(cz::heap_allocator()));

    // Use the kernel functions to standardize the path if they work.
#ifdef _WIN32
    {
        // Open the file in read mode.
        HANDLE handle = CreateFile(user_path_nt.buffer(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            CZ_DEFER(CloseHandle(handle));

            cz::String buffer = {};
            buffer.reserve(allocator, MAX_PATH);
            while (1) {
                // Get the standardized file name.
                DWORD res = GetFinalPathNameByHandleA(handle, buffer.buffer(), buffer.cap(), 0);

                if (res <= 0) {
                    // Failure so stop.
                    break;
                } else if (res < buffer.cap()) {
                    // Success.
                    buffer.set_len(res);

                    // Remove the "\\?\" prefix.
                    buffer.remove(0, 4);

                    cz::path::convert_to_forward_slashes(buffer.buffer(), buffer.len());
                    return buffer;
                } else {
                    // Retry with capacity as res.
                    buffer.reserve(allocator, res);
                }
            }
            buffer.drop(allocator);
        }
    }
#elif _BSD_SOURCE || _XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED
    // ^ Feature test for realpath.
    {
        char* ptr = realpath(user_path_nt.buffer(), nullptr);
        if (ptr) {
            // If we're using the heap allocator then we don't
            // need to reallocate since realpath uses malloc.
            if (allocator.vtable->alloc == cz::heap_allocator_alloc) {
                size_t len = strlen(ptr);
                return {ptr, len, len + 1};
            } else {
                CZ_DEFER(free(ptr));
                return cz::Str{ptr}.duplicate_null_terminate(allocator);
            }
        }
    }
#endif

    // Fallback to doing it ourselves.
    cz::path::convert_to_forward_slashes(user_path_nt.buffer(), user_path_nt.len());

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    cz::path::make_absolute(user_path_nt, cz::heap_allocator(), &path);
    if (path[path.len() - 1] == '/') {
        path.pop();
    }
    path.null_terminate();

    cz::String result = {};
    result.reserve(allocator, path.len());

#ifdef _WIN32
    // Todo: support symbolic links on Windows.

    // Append drive as uppercase.
    CZ_ASSERT(isalpha(path[0]));
    CZ_ASSERT(path[1] == ':');
    CZ_ASSERT(path[2] == '/');
    result.push(toupper(path[0]));
    result.push(':');

    // Only append the forward slash now if there are no components.
    if (path.len() == 3) {
        result.push('/');
    }

    // Step through each component of the path and fix the capitalization.
    size_t start = 3;
    while (1) {
        // Advance over forward slashes.
        while (start < path.len() && path[start] == '/') {
            ++start;
        }
        if (start == path.len()) {
            break;
        }

        // Find end of component.
        size_t end = start;
        while (end < path.len() && path[end] != '/') {
            ++end;
        }

        // Temporarily terminate the string at the end point.
        char swap = '\0';
        if (end < path.len()) {
            std::swap(swap, path[end]);
        }

        // Find the file on disk.
        WIN32_FIND_DATAA find_data;
        HANDLE handle = FindFirstFile(path.buffer(), &find_data);

        if (end < path.len()) {
            std::swap(swap, path[end]);
        }

        // If the find failed then just use the rest of the path as is.
        if (handle == INVALID_HANDLE_VALUE) {
            cz::Str rest_of_path = path.slice_start(start);
            result.reserve(allocator, rest_of_path.len + 1);
            result.push('/');
            result.append(rest_of_path);
            break;
        }

        FindClose(handle);

        // The find succeeded so get the proper component spelling and append it.
        cz::Str proper_component = find_data.cFileName;
        result.reserve(allocator, proper_component.len + 1);
        result.push('/');
        result.append(proper_component);

        start = end;
    }
#else
    // Path stores the components we have already dereferenced.
    result.reserve(allocator, path.len());

    // path stores the path we are trying to test.
    // temp_path will store the result of one readlink call.
    cz::String temp_path = {};
    temp_path.reserve(cz::heap_allocator(), path.len());
    CZ_DEFER(temp_path.drop(cz::heap_allocator()));

    // Try to dereference each component of the path as a symbolic link.  If any
    // component is a symbolic link it and the ones before it are replaced by
    // the link's contents.  Thus we iterate the path in reverse.

    while (1) {
        // Try to read the link.
        ssize_t res;
        const size_t max_dereferences = 5;
        size_t dereference_count = 0;
        while (1) {
            // Dereference the symbolic link.
            res = readlink(path.buffer(), temp_path.buffer(), temp_path.cap());

            // If we had an error, stop.
            if (res < 0) {
                break;
            }

            // Retry with a bigger buffer.
            if ((size_t)res == temp_path.cap()) {
                temp_path.reserve_total(cz::heap_allocator(), temp_path.cap() * 2);
                continue;
            }

            // Store the result in path.
            temp_path.set_len(res);
            if (cz::path::is_absolute(temp_path)) {
                temp_path.null_terminate();
                std::swap(temp_path, path);
            } else {
                path.reserve(cz::heap_allocator(), 4 + temp_path.len());
                path.append("/../");
                path.append(temp_path);
                size_t len = path.len();
                cz::path::flatten(path.buffer(), &len);
                path.set_len(len);
                path.null_terminate();
            }

            // Prevent infinite loops by stopping after a set count.
            if (dereference_count == max_dereferences) {
                break;
            }

            // Try dereferencing again.
            ++dereference_count;
        }

        size_t offset = 0;
        // Advance through the text part of the component.
        while (offset < path.len() && path[path.len() - offset - 1] != '/') {
            ++offset;
        }
        // Advance through forward slashes.
        while (offset < path.len() && path[path.len() - offset - 1] == '/') {
            ++offset;
        }

        // Push the component onto the path.
        result.reserve(allocator, offset);
        result.insert(0, path.slice_start(path.len() - offset));

        if (offset >= path.len()) {
            break;
        }

        // And chop the component off the path.
        path.set_len(path.len() - offset);
        path.null_terminate();
    }
#endif

    result.reserve(allocator, 1);
    result.null_terminate();
    return result;
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

    cz::String path = standardize_path(cz::heap_allocator(), user_path);
    CZ_DEFER(path.drop(cz::heap_allocator()));

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

    if (save_contents(&buffer->contents, path.buffer(), buffer->use_carriage_returns)) {
        buffer->mark_saved();
        return true;
    }

    return false;
}

bool save_contents_cr(const Contents* contents, cz::Output_File file) {
    for (size_t bucket = 0; bucket < contents->buckets.len(); ++bucket) {
        if (file.write_add_carriage_returns(contents->buckets[bucket].elems,
                                            contents->buckets[bucket].len) < 0) {
            return false;
        }
    }
    return true;
}

bool save_contents_no_cr(const Contents* contents, cz::Output_File file) {
    for (size_t bucket = 0; bucket < contents->buckets.len(); ++bucket) {
        if (file.write_binary(contents->buckets[bucket].elems, contents->buckets[bucket].len) < 0) {
            return false;
        }
    }
    return true;
}

bool save_contents(const Contents* contents, cz::Output_File file, bool use_carriage_returns) {
    if (use_carriage_returns) {
        return save_contents_cr(contents, file);
    } else {
        return save_contents_no_cr(contents, file);
    }
}

bool save_contents_cr(const Contents* contents, const char* path) {
    cz::Output_File file;
    if (!file.open(path)) {
        return false;
    }
    CZ_DEFER(file.close());

    return save_contents_cr(contents, file);
}

bool save_contents_no_cr(const Contents* contents, const char* path) {
    cz::Output_File file;
    if (!file.open(path)) {
        return false;
    }
    CZ_DEFER(file.close());

    return save_contents_no_cr(contents, file);
}

bool save_contents(const Contents* contents, const char* path, bool use_carriage_returns) {
    if (use_carriage_returns) {
        return save_contents_cr(contents, path);
    } else {
        return save_contents_no_cr(contents, path);
    }
}

bool save_contents_to_temp_file_cr(const Contents* contents, cz::Input_File* fd) {
    char temp_file_buffer[L_tmpnam];
    tmpnam(temp_file_buffer);
    if (!save_contents_cr(contents, temp_file_buffer)) {
        return false;
    }
    // Todo: don't open the file twice, instead open it once in read/write mode and reset the head.
    return fd->open(temp_file_buffer);
}

bool save_contents_to_temp_file_no_cr(const Contents* contents, cz::Input_File* fd) {
    char temp_file_buffer[L_tmpnam];
    tmpnam(temp_file_buffer);
    if (!save_contents_no_cr(contents, temp_file_buffer)) {
        return false;
    }
    // Todo: don't open the file twice, instead open it once in read/write mode and reset the head.
    return fd->open(temp_file_buffer);
}

bool save_contents_to_temp_file(const Contents* contents,
                                cz::Input_File* fd,
                                bool use_carriage_returns) {
    if (use_carriage_returns) {
        return save_contents_to_temp_file_cr(contents, fd);
    } else {
        return save_contents_to_temp_file_no_cr(contents, fd);
    }
}

}
