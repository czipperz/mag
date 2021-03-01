#pragma once

namespace cz {
struct Str;
struct Input_File;
struct Output_File;
struct Result;
struct File_Time;
}

namespace mag {

struct Buffer;
struct Editor;
struct Client;
struct Contents;
struct Buffer_Id;

bool check_out_of_date_and_update_file_time(const char* path, cz::File_Time* file_time);

cz::Result reload_directory_buffer(Buffer* buffer);

void open_file(Editor* editor, Client* client, cz::Str user_path);

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

bool find_buffer_by_path(Editor* editor, Client* client, cz::Str path, Buffer_Id* buffer_id);

/// Find temporary buffer by parsing the path.
///
/// `path` should be of the style `NAME` or `NAME (DIRECTORY)`.
bool find_temp_buffer(Editor* editor, Client* client, cz::Str path, Buffer_Id* buffer_id);

}
