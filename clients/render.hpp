#pragma once

#include "cell.hpp"
#include "client.hpp"
#include "editor.hpp"
#include "mini_buffer_results.hpp"
#include "window.hpp"
#include "window_cache.hpp"

namespace mag {
namespace client {

void render_to_cells(Cell* cells,
                     Window_Cache** window_cache,
                     Mini_Buffer_Results* mini_buffer_results,
                     int total_rows,
                     int total_cols,
                     Editor* editor,
                     Client* client);

}
}
