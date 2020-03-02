#pragma once

namespace cz {
struct Str;
}

namespace mag {

struct Editor;
struct Client;

void open_file(Editor* editor, Client* client, cz::Str user_path);

}
