#pragma once

namespace cz {
struct Input_File;
}

namespace mag {
struct Client;
struct Buffer;

int apply_diff_file(Client* client, Buffer* buffer, cz::Input_File file);

void reload_file(Client* client, Buffer* buffer);

}
