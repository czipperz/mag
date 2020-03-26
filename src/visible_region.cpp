#include "visible_region.hpp"

#include "contents.hpp"
#include "movement.hpp"
#include "window.hpp"

namespace mag {
namespace client {

void compute_visible_start(Window* window, Contents_Iterator* line_start_iterator) {
    ZoneScoped;

    for (size_t rows = 0; rows < window->rows - 2;) {
        Contents_Iterator next_line_start_iterator = *line_start_iterator;
        backward_line(&next_line_start_iterator);
        if (next_line_start_iterator.position == line_start_iterator->position) {
            CZ_DEBUG_ASSERT(line_start_iterator->position == 0);
            break;
        }

        int line_rows =
            (line_start_iterator->position - next_line_start_iterator.position + window->cols - 1) /
            window->cols;
        *line_start_iterator = next_line_start_iterator;

        rows += line_rows;
    }
}

void compute_visible_end(Window* window, Contents_Iterator* line_start_iterator) {
    ZoneScoped;

    for (size_t rows = 0; rows < window->rows - 1;) {
        Contents_Iterator next_line_start_iterator = *line_start_iterator;
        forward_line(&next_line_start_iterator);
        if (next_line_start_iterator.position == line_start_iterator->position) {
            break;
        }

        int line_rows =
            (next_line_start_iterator.position - line_start_iterator->position + window->cols - 1) /
            window->cols;
        *line_start_iterator = next_line_start_iterator;

        rows += line_rows;
    }
}

}
}
