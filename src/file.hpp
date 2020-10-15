#pragma once

namespace cz {
struct Str;
struct Input_File;
}

namespace mag {

struct Buffer;
struct Editor;
struct Client;
struct Contents;
struct Buffer_Id;

void open_file(Editor* editor, Client* client, cz::Str user_path);

bool save_buffer(Buffer* buffer);

bool save_contents(const Contents* contents, const char* path);

bool save_contents_to_temp_file(const Contents* contents, cz::Input_File* fd);

bool find_buffer_by_path(Editor* editor, Client* client, cz::Str path, Buffer_Id* buffer_id);

}
