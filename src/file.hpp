#pragma once

#include <cz/arc.hpp>
#include <cz/option.hpp>
#include <cz/path.hpp>
#include "buffer.hpp"

namespace cz {
struct Allocator;
struct Str;
struct String;
struct Input_File;
struct Output_File;
struct File_Time;
}

namespace mag {
struct Buffer_Handle;
struct Editor;
struct Client;
struct Contents;
struct Buffer_Id;
struct Asynchronous_Job;

bool check_out_of_date_and_update_file_time(const char* path, cz::File_Time* file_time);

bool reload_directory_buffer(Buffer* buffer);

/// If `user_path` isn't open, open it in a `Buffer` and replace the current `Window`.
///
/// The `user_path` does *not* need to be standardized as `open_file` will handle that.
bool open_file(Editor* editor, Client* client, cz::Str user_path);

/// Parse a "file arg" of the form `file` or `file:line` or `file:line:column`.
/// `*line` and `*column` are not modified if they are not present.
/// Returns `true` if `*line` is present.
///
/// Note that both lines and columns are formatted starting
/// at 1 but are handled internally starting at 0.
bool parse_file_arg(cz::Str user_arg, cz::Str* file, uint64_t* line, uint64_t* column);

/// Combines `parse_file_arg`, `open_file`, and then navigating to the requested point.
bool open_file_arg(Editor* editor, Client* client, cz::Str user_arg);

/// Create an asynchronous job that opens a file at the specified line and column.
///
/// `path` must be a heap-allocated string that is at least 1
/// character long.  This passes ownership of the string to the job.
///
/// Before calling this function you must check that the file hasn't already been opened.
Asynchronous_Job job_open_file(cz::String path, uint64_t line, uint64_t column, size_t index);

bool save_buffer(Buffer* buffer);
bool save_buffer_to(const Buffer* buffer, cz::Output_File file);
bool save_buffer_to(const Buffer* buffer, const char* path);
bool save_buffer_to_temp_file(const Buffer* buffer, cz::Input_File* fd);

bool save_contents(const Contents* contents, cz::Output_File file, bool use_carriage_returns);
bool save_contents(const Contents* contents, const char* path, bool use_carriage_returns);
bool save_contents_to_temp_file(const Contents* contents,
                                cz::Input_File* fd,
                                bool use_carriage_returns);

using cz::path::standardize_path;

namespace Load_File_Result_ {
enum Load_File_Result {
    SUCCESS,
    DOESNT_EXIST,
    FAILURE,
};
}
using Load_File_Result_::Load_File_Result;

/// Find or open a buffer.  Note that returning `DOESNT_EXIST` will still create a buffer.
///
/// Doesn't increment the reference count.
///
/// Standardizes the user_path internally.
Load_File_Result open_file_buffer(Editor* editor, cz::Str user_path, cz::Arc<Buffer_Handle>* handle_out);

/// Find a buffer by its path.  The path must be standardized with `standardize_path`.
///
/// Doesn't increment the reference count.
bool find_buffer_by_path(Editor* editor, cz::Str path, cz::Arc<Buffer_Handle>* handle_out);

/// Parse a rendered name (`path`) generated by `Buffer::render_name` back into a `name`,
/// `directory`, and `type`.  Returns `false` on invalid input, but still fills all three fields.
bool parse_rendered_buffer_name(cz::Str path,
                                cz::Str* name,
                                cz::Str* directory,
                                Buffer::Type* type);

/// Find temporary buffer by parsing the `path`.
///
/// `path` should be of the style `NAME` or `NAME (DIRECTORY)`.
///
/// Doesn't increment the reference count.
bool find_temp_buffer(Editor* editor,
                      Client* client,
                      cz::Str path,
                      cz::Arc<Buffer_Handle>* handle_out);

/// Find temporary buffer with a matching `name` and `directory`.
///
/// Doesn't increment the reference count.
bool find_temp_buffer(Editor* editor,
                      Client* client,
                      cz::Str name,
                      cz::Option<cz::Str> directory,
                      cz::Arc<Buffer_Handle>* handle_out);

/// Format date as `YYYY/MM/DD HH:MM:SS`.
void format_date(const cz::Date& date, char buffer[20]);

}
