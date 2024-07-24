#define __STDC_WANT_LIB_EXT1__ 1
#include "file.hpp"

#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <algorithm>
#include <cz/allocator.hpp>
#include <cz/bit_array.hpp>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/directory.hpp>
#include <cz/file.hpp>
#include <cz/parse.hpp>
#include <cz/path.hpp>
#include <cz/sort.hpp>
#include <cz/string.hpp>
#include <cz/util.hpp>
#include <tracy/Tracy.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "config.hpp"
#include "diff.hpp"
#include "editor.hpp"
#include "movement.hpp"
#include "program_info.hpp"
#include "server.hpp"
#include "tracy_format.hpp"
#include "visible_region.hpp"

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

/// Determine if we can write to the file by trying to open it in write mode.
static bool file_exists_and_can_write(const char* path) {
    ZoneScoped;
    ZoneText(path, strlen(path));

    cz::File_Descriptor file;
    CZ_DEFER(file.close());

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    file.handle =
        CreateFile(path, GENERIC_WRITE, 0, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
#else
    file.handle = ::open(path, O_WRONLY);
#endif

    return file.is_open();
}

static bool errno_is_noent() {
#ifdef _WIN32
    auto error = GetLastError();
    return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
#else
    return (errno == ENOENT);
#endif
}

Load_File_Result load_text_file(Buffer* buffer, cz::Input_File file) {
    cz::Carriage_Return_Carry carry;
    char buf[4096];
    while (1) {
        int64_t res = file.read(buf, sizeof(buf));
        if (res > 0) {
            cz::Str str = {buf, (size_t)res};
            const char* newline = str.find('\n');
            if (newline) {
                // Once we read one line, we can determine if this file in particular should use
                // carriage returns.
                buffer->use_carriage_returns = newline != buf && newline[-1] == '\r';

                cz::strip_carriage_returns(buf, &str.len, &carry);
                buffer->contents.append(str);
                goto read_continue;
            }
            buffer->contents.append(str);
        } else if (res == 0) {
            return Load_File_Result::SUCCESS;
        } else {
            return Load_File_Result::FAILURE;
        }
    }

read_continue:
    while (1) {
        int64_t res = file.read_strip_carriage_returns(buf, sizeof(buf), &carry);
        if (res > 0) {
            buffer->contents.append({buf, (size_t)res});
        } else if (res == 0) {
            return Load_File_Result::SUCCESS;
        } else {
            return Load_File_Result::FAILURE;
        }
    }
}

static bool load_directory(Buffer* buffer, cz::String* path) {
    if (!path->ends_with('/')) {
        path->push('/');
    }

    *buffer = {};
    buffer->type = Buffer::DIRECTORY;
    buffer->directory = path->clone_null_terminate(cz::heap_allocator());
    buffer->name = cz::Str(".").clone(cz::heap_allocator());
    buffer->read_only = true;

    bool result = reload_directory_buffer(buffer);

    if (!result) {
        buffer->contents.drop();
        buffer->name.drop(cz::heap_allocator());
        buffer->directory.drop(cz::heap_allocator());
        path->pop();
        path->null_terminate();
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

void format_date(const cz::Date& date, char buffer[21]) {
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

static Load_File_Result load_path_in_buffer(Buffer* buffer, cz::String* path) {
    // Try reading it as a directory, then if that fails read it as a file.  On
    // linux, opening it as a file will succeed even if it is a directory.  Then
    // reading the file will cause an error.
    if (load_directory(buffer, path)) {
        return Load_File_Result::SUCCESS;
    }

    *buffer = {};
    buffer->type = Buffer::FILE;

    cz::Str directory, name = *path;
    if (name.split_after_last('/', &directory, &name)) {
        buffer->directory = directory.clone_null_terminate(cz::heap_allocator());
    }
    buffer->name = name.clone(cz::heap_allocator());

    buffer->use_carriage_returns = custom::default_use_carriage_returns;

    buffer->read_only = !file_exists_and_can_write(path->buffer);

    cz::Input_File file;
    if (!file.open(path->buffer)) {
        // Failed to open so the file either doesn't exist or isn't readable.
        if (errno_is_noent()) {
            // Doesn't exist.
            buffer->read_only = false;
            return Load_File_Result::DOESNT_EXIST;
        } else {
            // Either permission error or spurious failure.
            return Load_File_Result::FAILURE;
        }
    }
    CZ_DEFER(file.close());

    return load_text_file(buffer, file);
}

static void start_syntax_highlighting(Editor* editor, cz::Arc<Buffer_Handle> handle) {
    {
        WITH_BUFFER_HANDLE(handle);
        // Mark that we started syntax highlighting.
        buffer->token_cache.generate_check_points_until(buffer, 0);

        TracyFormat(message, len, 1024, "Start syntax highlighting: %.*s", (int)buffer->name.len,
                    buffer->name.buffer);
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
    Load_File_Result result = load_path_in_buffer(&buffer, &data->path);
    if (result != Load_File_Result::SUCCESS) {
        if (result == Load_File_Result::DOESNT_EXIST) {
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

bool find_buffer_by_path(Editor* editor, cz::Str path, cz::Arc<Buffer_Handle>* handle_out) {
    if (path.len == 0) {
        return false;
    }

    cz::Str directory;
    cz::Str name;
    Buffer::Type type;
    if (!parse_rendered_buffer_name(path, &name, &directory, &type)) {
        return false;
    }

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

bool parse_rendered_buffer_name(cz::Str path,
                                cz::Str* name,
                                cz::Str* directory,
                                Buffer::Type* type) {
    if (path.starts_with('*')) {
        const char* ptr = path.find("* (");
        if (ptr) {
            *name = path.slice_end(ptr + 1);
            *directory = path.slice(ptr + 3, path.len - 1);
        } else {
            *name = path;
            *directory = {};
        }

        *type = Buffer::TEMPORARY;

        return name->starts_with('*') && name->ends_with('*');
    } else {
        if (!path.split_after_last('/', directory, name)) {
            *name = path;
            *directory = {};
            *type = Buffer::FILE;
            return false;
        }

        if (*name == ".") {
            *type = Buffer::DIRECTORY;
        } else {
            *type = Buffer::FILE;
        }
        return true;
    }
}

bool find_temp_buffer(Editor* editor,
                      Client* client,
                      cz::Str path,
                      cz::Arc<Buffer_Handle>* handle_out) {
    cz::Str name;
    cz::Str directory;
    Buffer::Type type;
    if (!parse_rendered_buffer_name(path, &name, &directory, &type)) {
        client->show_message("Error: invalid path");
        return false;
    }

    if (type != Buffer::TEMPORARY) {
        client->show_message("Error: expected temporary file path");
        return false;
    }

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

Load_File_Result open_file_buffer(Editor* editor,
                                  cz::Str user_path,
                                  cz::Arc<Buffer_Handle>* handle_out) {
    ZoneScoped;

    cz::String path = standardize_path(cz::heap_allocator(), user_path);
    CZ_DEFER(path.drop(cz::heap_allocator()));

    TracyFormat(message, len, 1024, "open_file_buffer: %s", path.buffer);
    TracyMessage(message, len);

    if (find_buffer_by_path(editor, path, handle_out)) {
        return Load_File_Result::SUCCESS;
    }

    Buffer buffer;
    Load_File_Result result = load_path_in_buffer(&buffer, &path);
    if (result != Load_File_Result::FAILURE) {
        *handle_out = editor->create_buffer(buffer);
    }
    return result;
}

bool open_file(Editor* editor, Client* client, cz::Str user_path) {
    ZoneScoped;

    if (user_path.len == 0) {
        client->show_message("File path must not be empty");
        return false;
    }

    cz::String path = standardize_path(cz::heap_allocator(), user_path);
    CZ_DEFER(path.drop(cz::heap_allocator()));

    cz::Arc<Buffer_Handle> handle;
    Load_File_Result result = open_file_buffer(editor, path, &handle);
    if (result == Load_File_Result::DOESNT_EXIST) {
        client->show_message("File not found");
        // Still open empty file buffer.
    } else if (result == Load_File_Result::FAILURE) {
        client->show_message("Couldn't open file");
        return false;
    }

    client->set_selected_buffer(handle);

    {
        WITH_CONST_BUFFER_HANDLE(handle);
        cz::File_Time file_time = buffer->file_time;
        if (check_out_of_date_and_update_file_time(path.buffer, &file_time)) {
            Buffer* buffer_mut = handle->increase_reading_to_writing();
            buffer_mut->file_time = file_time;
            const char* message = reload_file(buffer_mut);
            if (message)
                client->show_message(message);
        }
    }

    start_syntax_highlighting(editor, handle);
    return true;
}

bool parse_file_arg(cz::Str arg, cz::Str* file_out, uint64_t* line_out, uint64_t* column_out) {
    ZoneScoped;

    // If the file exists then immediately open it.
    if (cz::file::exists(arg.buffer)) {
    open:
        *file_out = arg;
        return false;
    }

    // Test FILE:LINE.  If these tests fail then it's not of this form.  If the FILE component
    // doesn't exist then the file being opened just has a colon and a bunch of numbers in its path.
    const char* colon = arg.rfind(':');
    if (!colon) {
        goto open;
    }

    cz::Str line_string = arg.slice_start(colon + 1);
    uint64_t line = 0;
    if (cz::parse(line_string, &line) != (int64_t)line_string.len) {
        goto open;
    }

    cz::String path = arg.slice_end(colon).clone_null_terminate(cz::heap_allocator());
    CZ_DEFER(path.drop(cz::heap_allocator()));

    if (cz::file::exists(path.buffer)) {
        // Argument is of form FILE:LINE.
        *file_out = arg.slice_end(path.len);
        *line_out = line;
        return true;
    }

    // Test FILE:LINE:COLUMN.  If these tests fail then it's not of this
    // form.  If the FILE component doesn't exist then the file being
    // opened just has a colon and a bunch of numbers in its path.
    colon = path.rfind(':');
    if (!colon) {
        goto open;
    }

    cz::Str column_string = path.slice_start(colon + 1);
    uint64_t column = 0;
    if (cz::parse(column_string, &column) != (int64_t)column_string.len) {
        goto open;
    }
    cz::swap(line, column);

    path.len = colon - path.buffer;
    path.null_terminate();

    if (cz::file::exists(path.buffer)) {
        // Argument is of form FILE:LINE:COLUMN.
        *file_out = arg.slice_end(path.len);
        *line_out = line;
        *column_out = column;
        return true;
    }

    goto open;
}

bool open_file_arg(Editor* editor, Client* client, cz::Str user_arg) {
    cz::Str file;
    uint64_t line = 0, column = 0;
    bool has_line = parse_file_arg(user_arg, &file, &line, &column);

    if (!open_file(editor, client, file))
        return false;

    if (has_line) {
        WITH_CONST_SELECTED_BUFFER(client);
        Contents_Iterator iterator = iterator_at_line_column(buffer->contents, line, column);
        window->cursors[0].point = iterator.position;
        center_in_window(window, buffer->mode, editor->theme, iterator);
    }
    return true;
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
