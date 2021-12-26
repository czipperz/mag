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
                     bool* any_animated_scrolling);

void recalculate_mouse(const Theme& theme, Client* client);

}
}
