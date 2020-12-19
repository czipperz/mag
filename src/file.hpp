#pragma once

namespace cz {
struct Str;
struct Input_File;
struct Output_File;
struct Result;
}

namespace mag {

struct Buffer;
struct Editor;
struct Client;
struct Contents;
struct Buffer_Id;

bool is_directory(const char* path);

cz::Result reload_directory_buffer(Buffer* buffer);

void open_file(Editor* editor, Client* client, cz::Str user_path);

bool save_buffer(Buffer* buffer);

void save_contents(const Contents* contents, cz::Output_File file);
bool save_contents(const Contents* contents, const char* path);
bool save_contents_to_temp_file(const Contents* contents, cz::Input_File* fd);

void save_contents_binary(const Contents* contents, cz::Output_File file);
bool save_contents_binary(const Contents* contents, const char* path);
bool save_contents_to_temp_file_binary(const Contents* contents, cz::Input_File* fd);

bool find_buffer_by_path(Editor* editor, Client* client, cz::Str path, Buffer_Id* buffer_id);

/// Find temporary buffer by parsing the path.
///
/// `path` should be of the style `NAME` or `NAME (DIRECTORY)`.
bool find_temp_buffer(Editor* editor, Client* client, cz::Str path, Buffer_Id* buffer_id);

}
