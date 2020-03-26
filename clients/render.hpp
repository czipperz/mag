#pragma once

#include <stddef.h>
#include "cell.hpp"
#include "client.hpp"
#include "completion_results.hpp"
#include "editor.hpp"
#include "window.hpp"
#include "window_cache.hpp"

namespace mag {
namespace client {

void render_to_cells(Cell* cells,
                     Window_Cache** window_cache,
                     size_t total_rows,
                     size_t total_cols,
                     Editor* editor,
                     Client* client);

}
}
