#pragma once

#include <cz/arc.hpp>
#include <cz/option.hpp>
#include <cz/path.hpp>
#include "core/buffer.hpp"
#include "core/job.hpp"

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

namespace Load_File_Result_ {
enum Open_File_Result {
    SUCCESS,
    DOESNT_EXIST,
    FAILURE,
};
}
using Load_File_Result_::Open_File_Result;

bool check_out_of_date_and_update_file_time(const char* path, cz::File_Time* file_time);

bool reload_directory_buffer(Buffer* buffer);

Synchronous_Job open_file_callback_do_nothing();

/// Open the given file in a `Buffer` and replace the current `Window`.
/// The `user_path` does *not* need to be standardized as `open_file` will handle that.
///
/// If this returns `Open_File_Result::SUCCESS` then `callback` will be enqueued
/// to run after the contents are loaded.  Otherwise, `callback` will be killed.
Open_File_Result open_file(Editor* editor,
                           Client* client,
                           cz::Str user_path,
                           Synchronous_Job callback = open_file_callback_do_nothing());

/// Open file and position cursor at the line, column.
Open_File_Result open_file_at(Editor* editor,
                              Client* client,
                              cz::Str user_path,
                              uint64_t line,
                              uint64_t column);

/// Parse a "file arg" of the form `file` or `file:line` or `file:line:column`.  `*line` and
/// `*column` are not modified if they are not present.  Returns `true` if `*line` is present.
///
/// `parse_file_arg` will only find `line`/`column` if the path exists after removing the suffix
/// whereas `parse_file_arg_no_disk` will always find `line`/`column` if they are in the string.
bool parse_file_arg(cz::Str user_arg, cz::Str* file, uint64_t* line, uint64_t* column);
bool parse_file_arg_no_disk(cz::Str user_arg, cz::Str* file, uint64_t* line, uint64_t* column);

/// Combines `parse_file_arg`, `open_file_at`.
Open_File_Result open_file_arg(Editor* editor, Client* client, cz::Str user_arg);

/// Find or open a buffer.  Note that returning `DOESNT_EXIST` will
/// still create a buffer.  Doesn't increment the reference count.
///
/// This lower-level function is exposed to allow for opening files in the background.
///
/// `unprocessed_keys` will be re-enqueued into the
/// client's `key_chain` once the file has been loaded.
Open_File_Result open_file_buffer(Editor* editor,
                                  cz::Str standardized_path,
                                  cz::Arc<Buffer_Handle>* handle_out,
                                  cz::Vector<Key> unprocessed_keys = {},
                                  Synchronous_Job callback = open_file_callback_do_nothing());

struct Open_File_Callback_Goto_Line_Column {
    uint64_t window_id;
    uint64_t line;
    uint64_t column;

    static Open_File_Callback_Goto_Line_Column* create(uint64_t line, uint64_t column);
    Open_File_Result finish_setup(const Client* client, Open_File_Result);
};
/// After creating the job you must call `finish_setup` with the result of `open_file`!
Synchronous_Job open_file_callback_goto_line_column(Open_File_Callback_Goto_Line_Column* data);

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
