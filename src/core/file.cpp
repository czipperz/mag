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
#include <cz/process.hpp>
#include <cz/sort.hpp>
#include <cz/string.hpp>
#include <cz/util.hpp>
#include <tracy/Tracy.hpp>
#include "core/client.hpp"
#include "core/command_macros.hpp"
#include "core/diff.hpp"
#include "core/editor.hpp"
#include "core/movement.hpp"
#include "core/program_info.hpp"
#include "core/server.hpp"
#include "core/tracy_format.hpp"
#include "core/visible_region.hpp"
#include "custom/config.hpp"

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

////////////////////////////////////////////////////////////////////////////////
// Load text file
////////////////////////////////////////////////////////////////////////////////

/// Load a file as text, stripping carriage returns, and assigning `buffer->use_carriage_returns`.
static Job_Tick_Result load_text_file_chunk(Buffer* buffer,
                                            cz::Input_File file,
                                            cz::Carriage_Return_Carry* carry,
                                            bool* first_line) {
    char buf[4096];
    size_t remaining_iterations = 32;
    if (*first_line) {
        for (; remaining_iterations-- > 0;) {
            int64_t res = file.read(buf, sizeof(buf));
            if (res > 0) {
                cz::Str str = {buf, (size_t)res};
                const char* newline = str.find('\n');
                if (newline) {
                    // Once we read one line, we can determine if this file in particular should use
                    // carriage returns.
                    buffer->use_carriage_returns = newline != buf && newline[-1] == '\r';

                    cz::strip_carriage_returns(buf, &str.len, carry);
                    buffer->contents.append(str);
                    *first_line = false;
                    break;
                }
                buffer->contents.append(str);
            } else if (res == 0) {
                return Job_Tick_Result::FINISHED;
            } else {
                return Job_Tick_Result::STALLED;
            }
        }
    }

    for (; remaining_iterations-- > 0;) {
        int64_t res = file.read_strip_carriage_returns(buf, sizeof(buf), carry);
        if (res > 0) {
            buffer->contents.append({buf, (size_t)res});
        } else if (res == 0) {
            return Job_Tick_Result::FINISHED;
        } else {
            return Job_Tick_Result::STALLED;
        }
    }
    return Job_Tick_Result::MADE_PROGRESS;
}

struct Load_Text_File_Job_Data {
    cz::Arc_Weak<Buffer_Handle> buffer_handle;
    cz::Input_File file;
    cz::Carriage_Return_Carry carry;
    bool first_line;
    Synchronous_Job callback;
};
static void load_text_file_job_kill(void* _data) {
    Load_Text_File_Job_Data* data = (Load_Text_File_Job_Data*)_data;
    data->buffer_handle.drop();
    data->file.close();
    (*data->callback.kill)(data->callback.data);
    cz::heap_allocator().dealloc(data);
}
static Job_Tick_Result load_text_file_job_tick(Asynchronous_Job_Handler* handler, void* _data) {
    Load_Text_File_Job_Data* data = (Load_Text_File_Job_Data*)_data;
    cz::Arc<Buffer_Handle> buffer_handle;
    if (!data->buffer_handle.upgrade(&buffer_handle)) {
        load_text_file_job_kill(_data);
        return Job_Tick_Result::FINISHED;
    }
    CZ_DEFER(buffer_handle.drop());

    WITH_BUFFER_HANDLE(buffer_handle);
    Job_Tick_Result result =
        load_text_file_chunk(buffer, data->file, &data->carry, &data->first_line);
    if (result == Job_Tick_Result::FINISHED) {
        data->buffer_handle.drop();
        data->file.close();
        handler->add_synchronous_job(data->callback);
        cz::heap_allocator().dealloc(data);
    }
    return result;
}
static void start_loading_text_file(Editor* editor,
                                    cz::Arc<Buffer_Handle> buffer_handle,
                                    cz::Input_File file,
                                    Synchronous_Job callback) {
    Load_Text_File_Job_Data* data = cz::heap_allocator().alloc<Load_Text_File_Job_Data>();
    data->buffer_handle = buffer_handle.clone_downgrade();
    data->file = file;
    data->carry = {};
    data->first_line = true;
    data->callback = callback;

    Asynchronous_Job job;
    job.tick = load_text_file_job_tick;
    job.kill = load_text_file_job_kill;
    job.data = data;
    editor->add_asynchronous_job(job);
}

////////////////////////////////////////////////////////////////////////////////
// Load directory
////////////////////////////////////////////////////////////////////////////////

static bool load_directory(Buffer* buffer, cz::Str path) {
    if (!path.ends_with('/')) {
        buffer->directory.reserve(cz::heap_allocator(), path.len + 2);
        buffer->directory.append(path);
        buffer->directory.push('/');
        buffer->directory.null_terminate();
    } else {
        buffer->directory.reserve(cz::heap_allocator(), path.len + 1);
        buffer->directory.append(path);
        buffer->directory.null_terminate();
    }

    bool result = reload_directory_buffer(buffer);
    if (result) {
        buffer->type = Buffer::DIRECTORY;
        buffer->name = cz::Str(".").clone(cz::heap_allocator());
        buffer->read_only = true;
    } else {
        buffer->directory.drop(cz::heap_allocator());
        buffer->directory = {};
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

static Open_File_Result load_path_in_buffer(Editor* editor,
                                            cz::Arc<Buffer_Handle> buffer_handle,
                                            cz::Str path,
                                            Synchronous_Job callback) {
    WITH_BUFFER_HANDLE(buffer_handle);

    // Try reading it as a directory, then if that fails read it as a file.  On
    // linux, opening it as a file will succeed even if it is a directory.  Then
    // reading the file will cause an error.
    if (load_directory(buffer, path)) {
        return Open_File_Result::SUCCESS;
    }

    buffer->type = Buffer::FILE;

    cz::Str directory, name = path;
    if (name.split_after_last('/', &directory, &name)) {
        buffer->directory = directory.clone_null_terminate(cz::heap_allocator());
    }
    buffer->name = name.clone(cz::heap_allocator());

    buffer->use_carriage_returns = custom::default_use_carriage_returns;

    buffer->read_only = !file_exists_and_can_write(path.buffer);

    cz::Input_File file;
    if (!file.open(path.buffer)) {
        // Failed to open so the file either doesn't exist or isn't readable.
        if (errno_is_noent()) {
            // Doesn't exist.
            buffer->read_only = false;
            return Open_File_Result::DOESNT_EXIST;
        } else {
            // Either permission error or spurious failure.
            return Open_File_Result::FAILURE;
        }
    }
    CZ_DEFER(file.close());

    // Inflate compressed files.
    for (size_t i = 0; i < custom::compression_extensions_len; ++i) {
        const auto& ext = custom::compression_extensions[i];
        if (path.ends_with(ext.extension)) {
            buffer->read_only = true;
            cz::Process process;
            cz::Input_File std_out;
            CZ_DEFER(std_out.close());
            {
                cz::Process_Options options;
                options.std_in = file;
                if (!cz::create_process_output_pipe(&options.std_out, &std_out)) {
                    return Open_File_Result::FAILURE;
                }
                CZ_DEFER(options.std_out.close());
                if (!std_out.set_non_blocking()) {
                    return Open_File_Result::FAILURE;
                }
                cz::Str args[] = {ext.process};
                if (!process.launch_program(args, options)) {
                    return Open_File_Result::FAILURE;
                }
            }
            file.close();
            file = {};
            process.detach();  // TODO show stderr / exit code
            start_loading_text_file(editor, buffer_handle, std_out, callback);
            std_out = {};  // Prevent destroying.
            return Open_File_Result::SUCCESS;
        }
    }

    (void)file.set_non_blocking();
    start_loading_text_file(editor, buffer_handle, file, callback);
    file = {};  // Prevent destroying.
    return Open_File_Result::SUCCESS;
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

/// Find or open a buffer.  Note that returning `DOESNT_EXIST` will
/// still create a buffer.  Doesn't increment the reference count.
Open_File_Result open_file_buffer(Editor* editor,
                                  cz::Str path,
                                  cz::Arc<Buffer_Handle>* handle_out,
                                  Synchronous_Job callback) {
    ZoneScoped;
    TracyFormat(message, len, 1024, "open_file_buffer: %s", path.buffer);
    TracyMessage(message, len);

    if (find_buffer_by_path(editor, path, handle_out)) {
        editor->add_synchronous_job(callback);
        return Open_File_Result::SUCCESS;
    }

    cz::Arc<Buffer_Handle> buffer_handle = create_buffer_handle(Buffer{});
    Open_File_Result result = load_path_in_buffer(editor, buffer_handle, path, callback);
    if (result != Open_File_Result::SUCCESS) {
        (*callback.kill)(callback.data);
    }

    if (result == Open_File_Result::FAILURE) {
        buffer_handle.drop();
    } else {
        // Open the buffer even if file not found.
        editor->create_buffer(buffer_handle);
        *handle_out = buffer_handle;
    }

    return result;
}

Synchronous_Job open_file_callback_do_nothing() {
    Synchronous_Job job;
    job.tick = [](Editor*, Client*, void*) { return Job_Tick_Result::FINISHED; };
    job.kill = [](void*) {};
    job.data = nullptr;
    return job;
}

Open_File_Result open_file(Editor* editor,
                           Client* client,
                           cz::Str user_path,
                           Synchronous_Job callback) {
    ZoneScoped;

    if (user_path.len == 0) {
        client->show_message("File path must not be empty");
        return Open_File_Result::FAILURE;
    }

    cz::String path = standardize_path(cz::heap_allocator(), user_path);
    CZ_DEFER(path.drop(cz::heap_allocator()));

    cz::Arc<Buffer_Handle> handle;
    Open_File_Result result = open_file_buffer(editor, path, &handle, callback);
    if (result == Open_File_Result::DOESNT_EXIST) {
        client->show_message("File not found");
        // Still open empty file buffer.
    } else if (result == Open_File_Result::FAILURE) {
        client->show_message("Couldn't open file");
        return result;
    }

    client->set_selected_buffer(handle);

    start_syntax_highlighting(editor, handle);
    return result;
}

Open_File_Callback_Goto_Line_Column* Open_File_Callback_Goto_Line_Column::create(uint64_t line,
                                                                                 uint64_t column) {
    Open_File_Callback_Goto_Line_Column* data =
        cz::heap_allocator().alloc<Open_File_Callback_Goto_Line_Column>();
    CZ_ASSERT(data);
    data->line = line;
    data->column = column;
    return data;
}
Open_File_Result Open_File_Callback_Goto_Line_Column::finish_setup(const Client* client,
                                                                   Open_File_Result result) {
    if (result == Open_File_Result::SUCCESS)
        window_id = client->selected_normal_window->id;
    return result;
}
Synchronous_Job open_file_callback_goto_line_column(Open_File_Callback_Goto_Line_Column* data) {
    Synchronous_Job job;
    job.tick = [](Editor* editor, Client* client, void* _data) {
        Open_File_Callback_Goto_Line_Column* data = (Open_File_Callback_Goto_Line_Column*)_data;
        Window_Unified* window = client->find_window(data->window_id);
        if (window) {
            WITH_CONST_WINDOW_BUFFER(window);
            kill_extra_cursors(window, client);
            Contents_Iterator iterator =
                iterator_at_line_column(buffer->contents, data->line, data->column);
            window->cursors[0].point = iterator.position;
            center_in_window(window, buffer->mode, editor->theme, iterator);
            window->show_marks = false;
        }
        cz::heap_allocator().dealloc(data);
        return Job_Tick_Result::FINISHED;
    };
    job.kill = [](void* data) {
        cz::heap_allocator().dealloc((Open_File_Callback_Goto_Line_Column*)data);
    };
    job.data = data;
    return job;
}

Open_File_Result open_file_at(Editor* editor,
                              Client* client,
                              cz::Str file,
                              uint64_t line,
                              uint64_t column) {
    Open_File_Callback_Goto_Line_Column* data =
        Open_File_Callback_Goto_Line_Column::create(line, column);
    return data->finish_setup(
        client, open_file(editor, client, file, open_file_callback_goto_line_column(data)));
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

bool parse_file_arg_no_disk(cz::Str arg,
                            cz::Str* file_out,
                            uint64_t* line_out,
                            uint64_t* column_out) {
    ZoneScoped;

    const char* colon = arg.rfind(':');
    if (!colon) {
    no_line_number:
        *file_out = arg;
        return false;
    }

    // See if there are any numbers at all.
    uint64_t last_number;
    cz::Str last_number_string = arg.slice_start(colon + 1);
    if (cz::parse(last_number_string, &last_number) != (int64_t)last_number_string.len) {
        goto no_line_number;
    }

    arg = arg.slice_end(colon);
    colon = arg.rfind(':');
    if (!colon) {
    line_only:
        *file_out = arg;
        *line_out = last_number;
        return true;
    }

    // If there are two numbers then the first is the line number & the second is the column.
    uint64_t first_number;
    cz::Str first_number_string = arg.slice_start(colon + 1);
    if (cz::parse(first_number_string, &first_number) != (int64_t)first_number_string.len) {
        goto line_only;
    }

    *file_out = arg.slice_end(colon);
    *line_out = first_number;
    *column_out = last_number;
    return true;
}

Open_File_Result open_file_arg(Editor* editor, Client* client, cz::Str user_arg) {
    cz::Str file;
    uint64_t line = 0, column = 0;
    bool has_line = parse_file_arg(user_arg, &file, &line, &column);
    if (has_line) {
        return open_file_at(editor, client, file, line, column);
    } else {
        return open_file(editor, client, file);
    }
}

bool save_buffer(Buffer* buffer) {
    // Disable saving compressed files because they we don't currently support deflation.
    for (size_t i = 0; i < custom::compression_extensions_len; ++i) {
        const auto& ext = custom::compression_extensions[i];
        if (buffer->name.ends_with(ext.extension))
            return false;
    }

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
