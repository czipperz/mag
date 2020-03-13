#pragma once

namespace cz {
struct Str;
}

namespace mag {

struct Editor;
struct Client;
struct Contents;
struct Buffer_Id;

void open_file(Editor* editor, Client* client, cz::Str user_path);

bool save_contents(const Contents* contents, const char* path);

bool find_buffer_by_path(Editor* editor, Client* client, cz::Str path, Buffer_Id* buffer_id);

}
