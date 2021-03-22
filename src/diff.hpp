#pragma once

namespace cz {
struct Input_File;
}

namespace mag {
struct Client;
struct Buffer;
struct Editor;

int apply_diff_file(Editor* editor, Client* client, Buffer* buffer, cz::Input_File file);

void reload_file(Editor* editor, Client* client, Buffer* buffer);

}
