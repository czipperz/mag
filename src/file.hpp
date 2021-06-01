#pragma once

#include <cz/arc.hpp>
#include <cz/option.hpp>

namespace cz {
struct Allocator;
struct Str;
struct String;
struct Input_File;
struct Output_File;
struct Result;
struct File_Time;
}

namespace mag {
struct Buffer;
struct Buffer_Handle;
struct Editor;
struct Client;
struct Contents;
struct Buffer_Id;
struct Asynchronous_Job;

bool check_out_of_date_and_update_file_time(const char* path, cz::File_Time* file_time);

cz::Result reload_directory_buffer(Buffer* buffer);

/// If `user_path` isn't open, open it in a `Buffer` and replace the current `Window`.
///
/// The `user_path` does *not* need to be standardized as `open_file` will handle that.
void open_file(Editor* editor, Client* client, cz::Str user_path);

/// Create an asynchronous job that opens a file at the specified line and column.
///
/// `path` must be a heap-allocated string that is at least 1
/// character long.  This passes ownership of the string to the job.
///
/// Before calling this function you must check that the file hasn't already been opened.
Asynchronous_Job job_open_file(cz::String path, uint64_t line, uint64_t column);

bool save_buffer(Buffer* buffer);

bool save_contents_cr(const Contents* contents, cz::Output_File file);
bool save_contents_cr(const Contents* contents, const char* path);
bool save_contents_to_temp_file_cr(const Contents* contents, cz::Input_File* fd);

bool save_contents_no_cr(const Contents* contents, cz::Output_File file);
bool save_contents_no_cr(const Contents* contents, const char* path);
bool save_contents_to_temp_file_no_cr(const Contents* contents, cz::Input_File* fd);

bool save_contents(const Contents* contents, cz::Output_File file, bool use_carriage_returns);
bool save_contents(const Contents* contents, const char* path, bool use_carriage_returns);
bool save_contents_to_temp_file(const Contents* contents,
                                cz::Input_File* fd,
                                bool use_carriage_returns);

/// Standardize the path so there is one exact way to spell each path.
///
/// This is done by making the path absolute, converting back slashes
/// to forward slashes, and then dereferencing all symbolic links.
///
/// If the path starts with `~` then the `~` is replaced with the user home directory.
///
/// This also standardizes the capitalization of the path on Windows.
cz::String standardize_path(cz::Allocator allocator, cz::Str user_path);

/// Find a buffer by its path.  The path must be standardized with `standardize_path`.
///
/// Doesn't increment the reference count.
bool find_buffer_by_path(Editor* editor,
                         Client* client,
                         cz::Str path,
                         cz::Arc<Buffer_Handle>* handle_out);

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
}
