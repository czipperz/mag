#pragma once

#include "cell.hpp"
#include "client.hpp"
#include "editor.hpp"
#include "completion_results.hpp"
#include "window.hpp"
#include "window_cache.hpp"

namespace mag {
namespace client {

void render_to_cells(Cell* cells,
                     Window_Cache** window_cache,
                     Completion_Results* completion_results,
                     int total_rows,
                     int total_cols,
                     Editor* editor,
                     Client* client);

}
}
