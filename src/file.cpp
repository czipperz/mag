#define __STDC_WANT_LIB_EXT1__ 1
#include "file.hpp"

#include <time.h>
#include <Tracy.hpp>
#include <algorithm>
#include <cz/allocator.hpp>
#include <cz/bit_array.hpp>
#include <cz/char_type.hpp>
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
#include "tracy_format.hpp"

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
    // If we're on Windows, set the default to use carriage returns.
#ifdef _WIN32
    buffer->use_carriage_returns = true;
#else
    buffer->use_carriage_returns = false;
#endif

    cz::Input_File file;
    if (!file.open(path)) {
        return {1};
    }
    CZ_DEFER(file.close());

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
                                 cz::Arc<Buffer_Handle>* handle) {
    if (path_len == 0 || path[path_len - 1] != '/') {
        path[path_len++] = '/';
    }

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

    *handle = editor->create_buffer(buffer);
    return result;
}

static void sort_files(cz::Slice<cz::Str> files,
                       cz::File_Time* file_times,
                       cz::Bit_Array file_directories,
                       bool sort_names) {
    auto swap = [&](size_t i, size_t j) {
        if (i == j) {
            return;
        }

        cz::Str tfile = files[i];
        cz::File_Time tfile_time = file_times[i];
        bool tfile_directory = file_directories.get(i);

        files[i] = files[j];
        file_times[i] = file_times[j];
        if (file_directories.get(j)) {
            file_directories.set(i);
        } else {
            file_directories.unset(i);
        }

        files[j] = tfile;
        file_times[j] = tfile_time;
        if (tfile_directory) {
            file_directories.set(j);
        } else {
            file_directories.unset(j);
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

    cz::File_Time* file_times = cz::heap_allocator().alloc<cz::File_Time>(files.len());
    CZ_ASSERT(file_times);
    CZ_DEFER(cz::heap_allocator().dealloc(file_times, files.len()));

    cz::Bit_Array file_directories;
    file_directories.init(cz::heap_allocator(), files.len());
    CZ_DEFER(file_directories.drop(cz::heap_allocator(), files.len()));

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
            file_directories.set(i);
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

        if (file_directories.get(i)) {
            buffer->contents.append("/ ");
        } else {
            buffer->contents.append("  ");
        }

        buffer->contents.append(files[i]);
        buffer->contents.append("\n");
    }

    return cz::Result::ok();
}

static cz::Result load_path(Editor* editor,
                            char* path,
                            size_t path_len,
                            cz::Arc<Buffer_Handle>* handle) {
    // Try reading it as a directory, then if that fails read it as a file.  On
    // linux, opening it as a file will succeed even if it is a directory.  Then
    // reading the file will cause an error.
    if (load_directory(editor, path, path_len, handle).is_ok()) {
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
    *handle = editor->create_buffer(buffer);
    return result;
}

cz::String standardize_path(cz::Allocator allocator, cz::Str user_path) {
    cz::String user_path_nt = user_path.duplicate_null_terminate(cz::heap_allocator());
    CZ_DEFER(user_path_nt.drop(cz::heap_allocator()));

    // Dereference home directory.
    if (user_path_nt.starts_with("~")) {
        const char* user_home_path;
#ifdef _WIN32
        user_home_path = getenv("USERPROFILE");
#else
        user_home_path = getenv("HOME");
#endif

        if (user_home_path) {
            cz::Str home = user_home_path;
            user_path_nt.reserve(cz::heap_allocator(), home.len);
            user_path_nt.remove(0, 1);
            user_path_nt.insert(0, home);
            user_path_nt.null_terminate();
        }
    }

#ifndef _WIN32
    // Don't dereference any symbolic links in `/proc` because the symbolic
    // links are often broken.  Example usage is `mag <(git diff)` will open
    // `/proc/self/fd/%d` with the result of the subcommand (`git diff`).
    if (user_path_nt.starts_with("/proc/")) {
        cz::path::convert_to_forward_slashes(user_path_nt.buffer(), user_path_nt.len());

        cz::String path = {};
        cz::path::make_absolute(user_path_nt, allocator, &path);
        if (path[path.len() - 1] == '/') {
            path.pop();
        }
        path.null_terminate();
        return path;
    }
#endif

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
                DWORD res =
                    GetFinalPathNameByHandleA(handle, buffer.buffer(), (DWORD)buffer.cap(), 0);

                if (res <= 0) {
                    // Failure so stop.
                    break;
                } else if (res < buffer.cap()) {
                    // Success.
                    buffer.set_len(res);

                    // Remove the "\\?\" prefix.
                    buffer.remove(0, 4);

                    cz::path::convert_to_forward_slashes(buffer.buffer(), buffer.len());

                    buffer.null_terminate();
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
#ifdef _WIN32
    if (path.len() > 3 && path[path.len() - 1] == '/') {
        path.pop();
    }
#endif
    if (path[path.len() - 1] == '/') {
        path.pop();
    }
    path.null_terminate();

    cz::String result = {};
    result.reserve(allocator, path.len() + 1);

#ifdef _WIN32
    // Todo: support symbolic links on Windows.

    // Append drive as uppercase.
    CZ_ASSERT(cz::is_alpha(path[0]));
    CZ_ASSERT(path[1] == ':');
    CZ_ASSERT(path.len() == 2 || path[2] == '/');
    result.push(cz::to_upper(path[0]));
    result.push(':');

    // Only append the forward slash now if there are no components.
    if (path.len() <= 3) {
        result.push('/');
    }

    // Step through each component of the path and fix the capitalization.
    size_t start = 3;
    while (1) {
        // Advance over forward slashes.
        while (start < path.len() && path[start] == '/') {
            ++start;
        }
        if (start >= path.len()) {
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

bool find_buffer_by_path(Editor* editor,
                         Client* client,
                         cz::Str path,
                         cz::Arc<Buffer_Handle>* handle_out) {
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
        cz::Arc<Buffer_Handle> handle = editor->buffers[i];

        {
            WITH_CONST_BUFFER_HANDLE(handle);

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
        *handle_out = handle;
        return true;
    }
    return false;
}

bool find_temp_buffer(Editor* editor,
                      Client* client,
                      cz::Str path,
                      cz::Arc<Buffer_Handle>* handle_out) {
    cz::Str name;
    cz::Str directory = {};
    const char* ptr = path.find("* (");
    if (ptr) {
        name = {path.buffer, size_t(ptr + 1 - path.buffer)};
        cz::Str dir = {ptr + 3, size_t(path.end() - (ptr + 3) - 1)};
        directory = {dir};
    } else {
        name = path;
        if (name.ends_with("* ")) {
            name.len--;
        }
    }

    CZ_DEBUG_ASSERT(name.starts_with("*"));
    CZ_DEBUG_ASSERT(name.ends_with("*"));
    name.buffer++;
    name.len -= 2;

    return find_temp_buffer(editor, client, name, directory, handle_out);
}

bool find_temp_buffer(Editor* editor,
                      Client* client,
                      cz::Str name,
                      cz::Option<cz::Str> directory,
                      cz::Arc<Buffer_Handle>* handle_out) {
    cz::String directory_standard = {};
    CZ_DEFER(directory_standard.drop(cz::heap_allocator()));
    if (directory.is_present) {
        directory_standard = standardize_path(cz::heap_allocator(), directory.value);
        // Use the space for the null terminator to store the trailing forward slash instead.
        directory_standard.push('/');
    }

    for (size_t i = 0; i < editor->buffers.len(); ++i) {
        cz::Arc<Buffer_Handle> handle = editor->buffers[i];
        WITH_CONST_BUFFER_HANDLE(handle);

        if (buffer->type == Buffer::TEMPORARY && buffer->name.len() >= 2 &&
            buffer->name.slice(1, buffer->name.len() - 1) == name) {
            if (buffer->directory == directory_standard) {
                *handle_out = handle;
                return true;
            }
        }
    }

    return false;
}

void open_file(Editor* editor, Client* client, cz::Str user_path) {
    ZoneScoped;

    if (user_path.len == 0) {
        client->show_message(editor, "File path must not be empty");
        return;
    }

    cz::String path = standardize_path(cz::heap_allocator(), user_path);
    CZ_DEFER(path.drop(cz::heap_allocator()));

    TracyFormat(message, len, 1024, "open_path: %s", path.buffer());
    TracyMessage(message, len);

    cz::Arc<Buffer_Handle> handle;
    if (find_buffer_by_path(editor, client, path, &handle)) {
    } else if (load_path(editor, path.buffer(), path.len(), &handle).is_err()) {
        client->show_message(editor, "File not found");
        // Still open empty file buffer.
    }

    client->set_selected_buffer(handle->id);

    {
        WITH_BUFFER_HANDLE(handle);
        // Mark that we started syntax highlighting.
        buffer->token_cache.generate_check_points_until(buffer, 0);

        TracyFormat(message, len, 1024, "Start syntax highlighting: %.*s", (int)buffer->name.len(),
                    buffer->name.buffer());
        TracyMessage(message, len);
    }

    editor->add_asynchronous_job(job_syntax_highlight_buffer(handle.clone_downgrade()));
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
    if (!tmpnam(temp_file_buffer)) {
        return false;
    }
    if (!save_contents_cr(contents, temp_file_buffer)) {
        return false;
    }
    // Todo: don't open the file twice, instead open it once in read/write mode and reset the head.
    return fd->open(temp_file_buffer);
}

bool save_contents_to_temp_file_no_cr(const Contents* contents, cz::Input_File* fd) {
    char temp_file_buffer[L_tmpnam];
    if (!tmpnam(temp_file_buffer)) {
        return false;
    }
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
