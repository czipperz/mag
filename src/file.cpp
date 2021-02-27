#define __STDC_WANT_LIB_EXT1__ 1
#include "file.hpp"

#include <time.h>
#include <algorithm>
#include <cz/bit_array.hpp>
#include <cz/defer.hpp>
#include <cz/fs/directory.hpp>
#include <cz/fs/read_to_string.hpp>
#include <cz/path.hpp>
#include <cz/process.hpp>
#include <cz/sort.hpp>
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

bool is_directory(const char* path) {
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

static bool get_file_time(const char* path, void* file_time) {
#ifdef _WIN32
    HANDLE handle = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle != INVALID_HANDLE_VALUE) {
        CZ_DEFER(CloseHandle(handle));
        if (GetFileTime(handle, NULL, NULL, (FILETIME*)file_time)) {
            return true;
        }
    }
    return false;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    *(time_t*)file_time = st.st_mtime;
    return true;
#endif
}

bool is_out_of_date(const char* path, void* file_time) {
#ifdef _WIN32
    FILETIME new_ft;
#else
    time_t new_ft;
#endif

    if (!get_file_time(path, &new_ft)) {
        return false;
    }

    if (is_file_time_before(file_time, &new_ft)) {
#ifdef _WIN32
        *(FILETIME*)file_time = new_ft;
#else
        *(time_t*)file_time = new_ft;
#endif
        return true;
    }

    return false;
}

bool is_file_time_before(const void* file_time, const void* other_file_time) {
#ifdef _WIN32
    return CompareFileTime((const FILETIME*)file_time, (const FILETIME*)other_file_time) < 0;
#else
    return *(const time_t*)file_time < *(const time_t*)other_file_time;
#endif
}

void* get_file_time(const char* path) {
#ifdef _WIN32
    void* file_time = malloc(sizeof(FILETIME));
    CZ_ASSERT(file_time);
#else
    void* file_time = malloc(sizeof(time_t));
    CZ_ASSERT(file_time);
#endif

    if (get_file_time(path, file_time)) {
        return file_time;
    } else {
        free(file_time);
        return nullptr;
    }
}

#ifndef _WIN32
static void to_date(const struct tm* tm, Date* date) {
    date->year = tm->tm_year + 1900;
    date->month = tm->tm_mon + 1;
    date->day_of_month = tm->tm_mday;
    date->day_of_week = tm->tm_wday;
    date->hour = tm->tm_hour;
    date->minute = tm->tm_min;
    date->second = tm->tm_sec;
}
#endif

bool file_time_to_date_utc(const void* file_time, Date* date) {
#ifdef _WIN32
    SYSTEMTIME system_time;
    if (!FileTimeToSystemTime((const FILETIME*)file_time, &system_time)) {
        return false;
    }
    date->year = system_time.wYear;
    date->month = system_time.wMonth;
    date->day_of_month = system_time.wDay;
    date->day_of_week = system_time.wDayOfWeek;
    date->hour = system_time.wHour;
    date->minute = system_time.wMinute;
    date->second = system_time.wSecond;
    return true;
#else
    // Try to use thread safe versions of gmtime when possible.
#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _BSD_SOURCE || _SVID_SOURCE || _POSIX_SOURCE
    struct tm storage;
    struct tm* tm = gmtime_r((const time_t*)file_time, &storage);
#elif defined(__STDC_LIB_EXT1__)
    struct tm storage;
    struct tm* tm = gmtime_s((const time_t*)file_time, &storage);
#else
    struct tm* tm = gmtime((const time_t*)file_time);
#endif

    to_date(tm, date);
    return true;
#endif
}

bool file_time_to_date_local(const void* file_time, Date* date) {
#ifdef _WIN32
    FILETIME local_time;
    if (!FileTimeToLocalFileTime((const FILETIME*)file_time, &local_time)) {
        return false;
    }
    return file_time_to_date_utc(&local_time, date);
#else
    // Try to use thread safe versions of localtime when possible.
#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _BSD_SOURCE || _SVID_SOURCE || _POSIX_SOURCE
    struct tm storage;
    struct tm* tm = localtime_r((const time_t*)file_time, &storage);
#elif defined(__STDC_LIB_EXT1__)
    struct tm storage;
    struct tm* tm = localtime_s((const time_t*)file_time, &storage);
#else
    struct tm* tm = localtime((const time_t*)file_time);
#endif

    to_date(tm, date);
    return true;
#endif
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
                       void** file_times,
                       unsigned char* file_directories,
                       bool sort_names) {
    auto swap = [&](size_t i, size_t j) {
        if (i == j) {
            return;
        }

        cz::Str tfile = files[i];
        void* tfile_time = file_times[i];
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

    void** file_times = (void**)malloc(sizeof(void*) * files.len());
    CZ_DEFER({
        for (size_t i = 0; i < files.len(); ++i) {
            free(file_times[i]);
        }
        free(file_times);
    });

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

        file_times[i] = get_file_time(file.buffer());

        if (is_directory(file.buffer())) {
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
        Date date;
        if (file_times[i] && file_time_to_date_local(file_times[i], &date)) {
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
