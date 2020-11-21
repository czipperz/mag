#pragma once

#include <stddef.h>
#include "cell.hpp"
#include "client.hpp"
#include "completion.hpp"
#include "editor.hpp"
#include "window.hpp"
#include "window_cache.hpp"

namespace mag {
struct Server;

namespace client {

bool load_mini_buffer_completion_cache(Server* server, Client* client);

void process_buffer_external_updates(Editor* editor, Client* client, Window* window);

void render_to_cells(Cell* cells,
                     Window_Cache** window_cache,
                     size_t total_rows,
                     size_t total_cols,
                     Editor* editor,
                     Client* client);

}
}
