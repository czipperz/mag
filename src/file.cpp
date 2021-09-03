#define __STDC_WANT_LIB_EXT1__ 1
#include "file.hpp"

#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <Tracy.hpp>
#include <algorithm>
#include <cz/allocator.hpp>
#include <cz/bit_array.hpp>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/directory.hpp>
#include <cz/file.hpp>
#include <cz/path.hpp>
#include <cz/sort.hpp>
#include <cz/string.hpp>
#include <cz/util.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "config.hpp"
#include "editor.hpp"
#include "movement.hpp"
#include "program_info.hpp"
#include "server.hpp"
#include "tracy_format.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
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

bool open_existing(cz::Output_File* file, const char* path) {
    ZoneScoped;
    ZoneText(path, strlen(path));

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    void* h = CreateFile(path, GENERIC_WRITE, 0, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    file->handle = h;
    return true;
#else
    file->fd = ::open(path, O_WRONLY);
    return file->fd != -1;
#endif
}

static int load_file(Buffer* buffer, const char* path) {
    buffer->use_carriage_returns = custom::default_use_carriage_returns;

    // Determine if we can write to the file by trying to open it in write mode.
    {
        cz::Output_File file;
        CZ_DEFER(file.close());

        buffer->read_only = !open_existing(&file, path);
    }

    cz::Input_File file;
    if (!file.open(path)) {
        // Failed to open so the file either doesn't exist or isn't readable.
        bool dne;
#ifdef _WIN32
        auto error = GetLastError();
        dne = (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
#else
        dne = (errno == ENOENT);
#endif
        if (dne) {
            // Doesn't exist.
            buffer->read_only = false;
            return 1;
        } else {
            // Either permission error or spurious failure.
            return 2;
        }
    }
    CZ_DEFER(file.close());

    cz::Carriage_Return_Carry carry;
    char buf[1024];
    while (1) {
        int64_t res = file.read(buf, sizeof(buf));
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
            return 0;
        } else {
            return 1;
        }
    }

read_continue:
    while (1) {
        int64_t res = file.read_strip_carriage_returns(buf, sizeof(buf), &carry);
        if (res > 0) {
            buffer->contents.append({buf, (size_t)res});
        } else if (res == 0) {
            return 0;
        } else {
            return 1;
        }
    }
}

static bool load_directory(Buffer* buffer, char* path, size_t path_len) {
    if (path_len == 0 || path[path_len - 1] != '/') {
        path[path_len++] = '/';
    }

    *buffer = {};
    buffer->type = Buffer::DIRECTORY;
    buffer->directory = cz::Str(path, path_len).clone_null_terminate(cz::heap_allocator());
    buffer->name = cz::Str(".").clone(cz::heap_allocator());
    buffer->read_only = true;

    bool result = reload_directory_buffer(buffer);

    if (!result) {
        buffer->contents.drop();
        buffer->name.drop(cz::heap_allocator());
        buffer->directory.drop(cz::heap_allocator());
        path[--path_len] = '\0';
    }

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

void format_date(const cz::Date& date, char buffer[20]) {
    snprintf(buffer, 21, "%04d/%02d/%02d %02d:%02d:%02d", date.year, date.month, date.day_of_month,
             date.hour, date.minute, date.second);
}

bool reload_directory_buffer(Buffer* buffer) {
    cz::Buffer_Array buffer_array;
    buffer_array.init();
    CZ_DEFER(buffer_array.drop());

    cz::Vector<cz::Str> files = {};
    CZ_DEFER(files.drop(cz::heap_allocator()));

    if (!cz::files(cz::heap_allocator(), buffer_array.allocator(), buffer->directory.buffer,
                   &files)) {
        return false;
    }

    cz::File_Time* file_times = cz::heap_allocator().alloc<cz::File_Time>(files.len);
    CZ_ASSERT(file_times);
    CZ_DEFER(cz::heap_allocator().dealloc(file_times, files.len));

    cz::Bit_Array file_directories;
    file_directories.init(cz::heap_allocator(), files.len);
    CZ_DEFER(file_directories.drop(cz::heap_allocator(), files.len));

    cz::String file = {};
    CZ_DEFER(file.drop(cz::heap_allocator()));
    file.reserve(cz::heap_allocator(), buffer->directory.len);
    file.append(buffer->directory);

    for (size_t i = 0; i < files.len; ++i) {
        file.len = buffer->directory.len;
        file.reserve(cz::heap_allocator(), files[i].len + 1);
        file.append(files[i]);
        file.null_terminate();

        file_times[i] = {};
        cz::get_file_time(file.buffer, &file_times[i]);

        if (cz::file::is_directory(file.buffer)) {
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

    for (size_t i = 0; i < files.len; ++i) {
        cz::Date date;
        if (cz::file_time_to_date_local(file_times[i], &date)) {
            char date_string[21];
            format_date(date, date_string);
            buffer->contents.append(date_string);
        } else {
            buffer->contents.append("                   ");
        }

        if (file_directories.get(i)) {
            buffer->contents.append(" / ");
        } else {
            buffer->contents.append("   ");
        }

        buffer->contents.append(files[i]);
        buffer->contents.append("\n");
    }

    return true;
}

static int load_path_in_buffer(Buffer* buffer, char* path, size_t path_len) {
    // Try reading it as a directory, then if that fails read it as a file.  On
    // linux, opening it as a file will succeed even if it is a directory.  Then
    // reading the file will cause an error.
    if (load_directory(buffer, path, path_len)) {
        return 0;
    }

    *buffer = {};
    buffer->type = Buffer::FILE;

    cz::Str path_str = {path, path_len};
    const char* end_dir = path_str.rfind('/');
    if (end_dir) {
        ++end_dir;
        buffer->directory = path_str.slice_end(end_dir).clone_null_terminate(cz::heap_allocator());
        buffer->name = path_str.slice_start(end_dir).clone(cz::heap_allocator());
    } else {
        buffer->name = path_str.clone(cz::heap_allocator());
    }

    return load_file(buffer, path);
}

static void start_syntax_highlighting(Editor* editor, cz::Arc<Buffer_Handle> handle) {
    {
        WITH_BUFFER_HANDLE(handle);
        // Mark that we started syntax highlighting.
        buffer->token_cache.generate_check_points_until(buffer, 0);

        TracyFormat(message, len, 1024, "Start syntax highlighting: %.*s", (int)buffer->name.len,
                    buffer->name.buffer());
        TracyMessage(message, len);
    }

    editor->add_asynchronous_job(job_syntax_highlight_buffer(handle.clone_downgrade()));
}

struct Finish_Open_File_Sync_Data {
    Buffer buffer;
    uint64_t position;
    size_t index;
};

static void finish_open_file(Editor* editor,
                             Client* client,
                             const Buffer& buffer,
                             uint64_t position,
                             size_t index) {
    cz::Arc<Buffer_Handle> handle = editor->create_buffer(buffer);

    if (index > 0) {
        split_window(client, index % 2 == 0 ? Window::HORIZONTAL_SPLIT : Window::VERTICAL_SPLIT);
    }

    client->set_selected_buffer(handle);
    client->selected_normal_window->cursors[0].point = position;

    start_syntax_highlighting(editor, handle);

    {
        WITH_BUFFER_HANDLE(handle);
        push_jump(client->selected_normal_window, client, buffer);
    }
}

static Job_Tick_Result finish_open_file_sync_job_tick(Editor* editor, Client* client, void* _data) {
    ZoneScoped;

    Finish_Open_File_Sync_Data* data = (Finish_Open_File_Sync_Data*)_data;

    finish_open_file(editor, client, data->buffer, data->position, data->index);

    cz::heap_allocator().dealloc(data);
    return Job_Tick_Result::FINISHED;
}

static void finish_open_file_sync_job_kill(void* _data) {
    Finish_Open_File_Sync_Data* data = (Finish_Open_File_Sync_Data*)_data;
    data->buffer.drop();
    cz::heap_allocator().dealloc(data);
}

static Synchronous_Job job_finish_open_file_sync(Buffer buffer, uint64_t position, size_t index) {
    Finish_Open_File_Sync_Data* data = cz::heap_allocator().alloc<Finish_Open_File_Sync_Data>();
    data->buffer = buffer;
    data->position = position;
    data->index = index;

    Synchronous_Job job;
    job.data = data;
    job.tick = finish_open_file_sync_job_tick;
    job.kill = finish_open_file_sync_job_kill;
    return job;
}

struct Open_File_Async_Data {
    cz::String path;  // heap allocated
    uint64_t line;
    uint64_t column;
    size_t index;
};

static void open_file_job_kill(void* _data) {
    Open_File_Async_Data* data = (Open_File_Async_Data*)_data;
    data->path.drop(cz::heap_allocator());
    cz::heap_allocator().dealloc(data);
}

static Job_Tick_Result open_file_job_tick(Asynchronous_Job_Handler* handler, void* _data) {
    ZoneScoped;

    Open_File_Async_Data* data = (Open_File_Async_Data*)_data;

    Buffer buffer;
    int result = load_path_in_buffer(&buffer, data->path.buffer, data->path.len);
    if (result != 0) {
        if (result == 1) {
            handler->show_message("File not found");
            // Still open empty file buffer.
        } else {
            handler->show_message("Couldn't open file");

            // Returning here when there are multiple other files being opened asynchronously
            // allows for weird windowing states but they won't crash so it's ok for now.
            return Job_Tick_Result::FINISHED;
        }
    }

    uint64_t position = iterator_at_line_column(buffer.contents, data->line, data->column).position;

    // Finish by loading the buffer synchronously.  We *cannot* attempt to lock the state here
    // because it introduces a race condition where: the first job fails to lock and finishes via a
    // synchronous job then the second job successfully locks and finishes immediately.  This causes
    // the second job to finish before the first job which the code to finish doesn't handle.
    handler->add_synchronous_job(job_finish_open_file_sync(buffer, position, data->index));

    open_file_job_kill(data);
    return Job_Tick_Result::FINISHED;
}

Asynchronous_Job job_open_file(cz::String path, uint64_t line, uint64_t column, size_t index) {
    CZ_DEBUG_ASSERT(path.len > 0);

    Open_File_Async_Data* data = cz::heap_allocator().alloc<Open_File_Async_Data>();
    data->path = path;
    data->line = line;
    data->column = column;
    data->index = index;

    Asynchronous_Job job;
    job.data = data;
    job.tick = open_file_job_tick;
    job.kill = open_file_job_kill;
    return job;
}

static int load_path(Editor* editor, char* path, size_t path_len, cz::Arc<Buffer_Handle>* handle) {
    Buffer buffer;
    int result = load_path_in_buffer(&buffer, path, path_len);
    if (result != 2) {
        *handle = editor->create_buffer(buffer);
    }
    return result;
}

cz::String standardize_path(cz::Allocator allocator, cz::Str user_path) {
    cz::String user_path_nt = user_path.clone_null_terminate(cz::heap_allocator());
    CZ_DEFER(user_path_nt.drop(cz::heap_allocator()));

    // Dereference home directory.
    if (user_path_nt.starts_with("~")) {
        if (user_home_path) {
            cz::Str home = user_home_path;
            user_path_nt.reserve(cz::heap_allocator(), home.len);
            user_path_nt.remove(0);
            user_path_nt.insert(0, home);
            user_path_nt.null_terminate();
        }
    }

#ifndef _WIN32
    // Don't dereference any symbolic links in `/proc` because the symbolic
    // links are often broken.  Example usage is `mag <(git diff)` will open
    // `/proc/self/fd/%d` with the result of the subcommand (`git diff`).
    if (user_path_nt.starts_with("/proc/")) {
        cz::path::convert_to_forward_slashes(&user_path_nt);

        cz::String path = {};
        cz::path::make_absolute(user_path_nt, allocator, &path);
        if (path[path.len - 1] == '/') {
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
        HANDLE handle = CreateFile(user_path_nt.buffer, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            CZ_DEFER(CloseHandle(handle));

            cz::String buffer = {};
            buffer.reserve(allocator, MAX_PATH);
            while (1) {
                // Get the standardized file name.
                DWORD res = GetFinalPathNameByHandleA(handle, buffer.buffer, (DWORD)buffer.cap, 0);

                if (res <= 0) {
                    // Failure so stop.
                    break;
                } else if (res < buffer.cap) {
                    // Success.
                    buffer.len = res;

                    // Remove the "\\?\" prefix.
                    buffer.remove_many(0, 4);

                    cz::path::convert_to_forward_slashes(&buffer);

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
        char* ptr = realpath(user_path_nt.buffer, nullptr);
        if (ptr) {
            CZ_DEFER(free(ptr));
            return cz::Str{ptr}.clone_null_terminate(allocator);
        }
    }
#endif

    // Fallback to doing it ourselves.

#ifdef _WIN32
    cz::path::convert_to_forward_slashes(&user_path_nt);
#endif

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    cz::path::make_absolute(user_path_nt, cz::heap_allocator(), &path);
    if (path[path.len - 1] == '/') {
        path.pop();
    }
    path.null_terminate();

    cz::String result = {};
    result.reserve(allocator, path.len + 1);

#ifdef _WIN32
    // TODO: support symbolic links on Windows.

    // Append drive as uppercase.
    CZ_DEBUG_ASSERT(cz::is_alpha(path[0]));
    CZ_DEBUG_ASSERT(path[1] == ':');
    CZ_DEBUG_ASSERT(path.len == 2 || path[2] == '/');
    result.push(cz::to_upper(path[0]));
    result.push(':');

    // Only append the forward slash now if there are no components.
    if (path.len <= 3) {
        result.push('/');
    }

    // Step through each component of the path and fix the capitalization.
    size_t start = 3;
    while (1) {
        // Advance over forward slashes.
        while (start < path.len && path[start] == '/') {
            ++start;
        }
        if (start >= path.len) {
            break;
        }

        // Find end of component.
        size_t end = start;
        while (end < path.len && path[end] != '/') {
            ++end;
        }

        // Temporarily terminate the string at the end point.
        char swap = '\0';
        if (end < path.len) {
            cz::swap(swap, path[end]);
        }

        // Find the file on disk.
        WIN32_FIND_DATAA find_data;
        HANDLE handle = FindFirstFile(path.buffer, &find_data);

        if (end < path.len) {
            cz::swap(swap, path[end]);
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
    result.reserve(allocator, path.len);

    // path stores the path we are trying to test.
    // temp_path will store the result of one readlink call.
    cz::String temp_path = {};
    temp_path.reserve(cz::heap_allocator(), path.len);
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
            res = readlink(path.buffer, temp_path.buffer, temp_path.cap());

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
            temp_path.len = res;

            if (cz::path::is_absolute(temp_path)) {
                // Discard the directory of the symlink and since it is an absolute path.
                cz::swap(temp_path, path);

                // Pop off trailing forward slashes.
                while (path.ends_with('/')) {
                    path.pop();
                }
            } else {
                // Expand the symlink from the directory it is in.
                path.reserve(cz::heap_allocator(), temp_path.len + 5);
                path.append("/../");
                path.append(temp_path);
            }

            // The symlink may use relative paths so flatten it out now.
            cz::path::flatten(&path);
            path.null_terminate();

            // Prevent infinite loops by stopping after a set count.
            if (dereference_count == max_dereferences) {
                break;
            }

            // Try dereferencing again.
            ++dereference_count;
        }

        size_t offset = 0;
        // Advance through the text part of the component.
        while (offset < path.len && path[path.len - offset - 1] != '/') {
            ++offset;
        }
        // Advance through forward slashes.
        while (offset < path.len && path[path.len - offset - 1] == '/') {
            ++offset;
        }

        // Push the component onto the path.
        result.reserve(allocator, offset);
        result.insert(0, path.slice_start(path.len - offset));

        if (offset >= path.len) {
            break;
        }

        // And chop the component off the path.
        path.len = path.len - offset;
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
    Buffer::Type type = parse_rendered_buffer_name(path, &name, &directory);

    for (size_t i = 0; i < editor->buffers.len; ++i) {
        cz::Arc<Buffer_Handle> handle = editor->buffers[i];

        {
            WITH_CONST_BUFFER_HANDLE(handle);

            if (buffer->type == type && buffer->directory == directory && buffer->name == name) {
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

Buffer::Type parse_rendered_buffer_name(cz::Str path, cz::Str* name, cz::Str* directory) {
    if (path.starts_with('*')) {
        const char* ptr = path.find("* (");
        if (ptr) {
            *name = path.slice_end(ptr + 1);
            *directory = path.slice(ptr + 3, path.len - 1);
        } else {
            *name = path;
            *directory = {};
        }

        CZ_ASSERT(name->starts_with('*'));
        CZ_ASSERT(name->ends_with('*'));

        return Buffer::TEMPORARY;
    } else {
        const char* ptr = path.rfind('/');
        CZ_ASSERT(ptr);

        *name = path.slice_start(ptr + 1);
        *directory = path.slice_end(ptr + 1);

        if (*name == ".") {
            return Buffer::DIRECTORY;
        } else {
            return Buffer::FILE;
        }
    }
}

bool find_temp_buffer(Editor* editor,
                      Client* client,
                      cz::Str path,
                      cz::Arc<Buffer_Handle>* handle_out) {
    cz::Str name;
    cz::Str directory;

    Buffer::Type type = parse_rendered_buffer_name(path, &name, &directory);
    CZ_ASSERT(type == Buffer::TEMPORARY);

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

    for (size_t i = 0; i < editor->buffers.len; ++i) {
        cz::Arc<Buffer_Handle> handle = editor->buffers[i];
        WITH_CONST_BUFFER_HANDLE(handle);

        if (buffer->type == Buffer::TEMPORARY && buffer->name.len >= 2 &&
            buffer->name.slice(1, buffer->name.len - 1) == name) {
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
        client->show_message("File path must not be empty");
        return;
    }

    cz::String path = standardize_path(cz::heap_allocator(), user_path);
    CZ_DEFER(path.drop(cz::heap_allocator()));

    TracyFormat(message, len, 1024, "open_path: %s", path.buffer);
    TracyMessage(message, len);

    cz::Arc<Buffer_Handle> handle;
    if (find_buffer_by_path(editor, client, path, &handle)) {
    } else {
        int result = load_path(editor, path.buffer, path.len, &handle);
        if (result != 0) {
            if (result == 1) {
                client->show_message("File not found");
                // Still open empty file buffer.
            } else {
                client->show_message("Couldn't open file");
                return;
            }
        }
    }

    client->set_selected_buffer(handle);

    start_syntax_highlighting(editor, handle);
}

bool save_buffer(Buffer* buffer) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    if (!buffer->get_path(cz::heap_allocator(), &path)) {
        return false;
    }

    if (!save_buffer_to(buffer, path.buffer)) {
        return false;
    }

    buffer->mark_saved();
    return true;
}

bool save_buffer_to(const Buffer* buffer, cz::Output_File file) {
    return save_contents(&buffer->contents, file, buffer->use_carriage_returns);
}

bool save_buffer_to(const Buffer* buffer, const char* path) {
    return save_contents(&buffer->contents, path, buffer->use_carriage_returns);
}

bool save_buffer_to_temp_file(const Buffer* buffer, cz::Input_File* file) {
    return save_contents_to_temp_file(&buffer->contents, file, buffer->use_carriage_returns);
}

bool save_contents(const Contents* contents, cz::Output_File file, bool use_carriage_returns) {
    if (use_carriage_returns) {
        for (size_t bucket = 0; bucket < contents->buckets.len; ++bucket) {
            if (file.write_add_carriage_returns(contents->buckets[bucket].elems,
                                                contents->buckets[bucket].len) < 0) {
                return false;
            }
        }
        return true;
    } else {
        for (size_t bucket = 0; bucket < contents->buckets.len; ++bucket) {
            if (file.write(contents->buckets[bucket].elems, contents->buckets[bucket].len) < 0) {
                return false;
            }
        }
        return true;
    }
}

bool save_contents(const Contents* contents, const char* path, bool use_carriage_returns) {
    cz::Output_File file;
    if (!file.open(path)) {
        return false;
    }
    CZ_DEFER(file.close());

    return save_contents(contents, file, use_carriage_returns);
}

bool save_contents_to_temp_file(const Contents* contents,
                                cz::Input_File* fd,
                                bool use_carriage_returns) {
    char temp_file_buffer[L_tmpnam];
    if (!tmpnam(temp_file_buffer)) {
        return false;
    }

    if (!save_contents(contents, temp_file_buffer, use_carriage_returns)) {
        return false;
    }

    // TODO: don't open the file twice, instead open it once in read/write mode and reset the head.
    return fd->open(temp_file_buffer);
}

}
