#pragma once

#include <stddef.h>
#include "cell.hpp"
#include "client.hpp"
#include "completion.hpp"
#include "editor.hpp"
#include "screen_position.hpp"
#include "window.hpp"
#include "window_cache.hpp"

namespace mag {
struct Server;

namespace render {

bool load_mini_buffer_completion_cache(Server* server, Client* client);

void process_buffer_external_updates(Editor* editor, Client* client, Window* window);

void render_to_cells(Cell* cells,
                     Window_Cache** window_cache,
                     Window_Cache** mini_buffer_window_cache,
                     size_t total_rows,
                     size_t total_cols,
                     Editor* editor,
                     Client* client,
                     cz::Slice<Screen_Position_Query> spqs);

}
}
