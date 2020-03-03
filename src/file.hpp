#pragma once

namespace cz {
struct Str;
}

namespace mag {

struct Editor;
struct Client;
struct Contents;

void open_file(Editor* editor, Client* client, cz::Str user_path);

bool save_contents(const Contents* contents, const char* path);

}
