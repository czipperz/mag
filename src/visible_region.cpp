#include "visible_region.hpp"

#include "contents.hpp"
#include "movement.hpp"
#include "window.hpp"

namespace mag {

void compute_visible_start(Window* window, Contents_Iterator* iterator) {
    ZoneScoped;

    backward_char(iterator);

    size_t row = 0;
    size_t col = 0;
    size_t target_rows = window->rows;
    for (; !iterator->at_bob(); iterator->retreat()) {
        if (iterator->get() == '\n') {
            ++row;
            if (row >= target_rows) {
                end_of_line(iterator);
                forward_char(iterator);
                break;
            }
            col = 0;
        } else {
            ++col;
            if (col >= window->cols) {
                col -= window->cols;
                ++row;
                if (row >= target_rows) {
                    end_of_line(iterator);
                    forward_char(iterator);
                    break;
                }
            }
        }
    }
}

void compute_visible_end(Window* window, Contents_Iterator* iterator) {
    ZoneScoped;

    size_t row = 0;
    size_t col = 0;
    size_t target_rows = window->rows - 1;
    for (; !iterator->at_eob(); iterator->advance()) {
        if (iterator->get() == '\n') {
            ++row;
            if (row >= target_rows) {
                break;
            }
            col = 0;
        } else {
            ++col;
            if (col >= window->cols) {
                ++row;
                if (row >= target_rows) {
                    break;
                }
                col -= window->cols;
            }
        }
    }
}

void center_in_window(Window_Unified* window, Contents_Iterator iterator) {
    backward_char(&iterator);

    size_t row = 0;
    size_t col = 0;
    size_t target_rows = window->rows / 2;
    for (; !iterator.at_bob(); iterator.retreat()) {
        if (iterator.get() == '\n') {
            ++row;
            if (row >= target_rows) {
                start_of_line(&iterator);
                // forward_line(buffer->mode, &iterator);
                break;
            }
            col = 0;
        } else {
            ++col;
            if (col >= window->cols) {
                col -= window->cols;
                ++row;
                if (row >= target_rows) {
                    start_of_line(&iterator);
                    // forward_line(buffer->mode, &iterator);
                    break;
                }
            }
        }
    }

    window->start_position = iterator.position;
}

Contents_Iterator center_of_window(Window_Unified* window, const Contents* contents) {
    Contents_Iterator iterator = contents->iterator_at(window->start_position);
    size_t row = 0;
    size_t col = 0;
    size_t target_rows = window->rows / 2;
    for (; !iterator.at_eob(); iterator.advance()) {
        if (iterator.get() == '\n') {
            ++row;
            if (row >= target_rows) {
                iterator.advance();
                break;
            }
            col = 0;
        } else {
            ++col;
            if (col >= window->cols) {
                ++row;
                if (row >= target_rows) {
                    iterator.advance();
                    break;
                }
                col -= window->cols;
            }
        }
    }
    return iterator;
}

bool is_visible(Window_Unified* window, Contents_Iterator iterator) {
    if (iterator.position < window->start_position) {
        return false;
    }

    Contents_Iterator end = iterator;
    // Go to start position
    end.retreat_to(window->start_position);
    // Then advance to end of visible region
    compute_visible_end(window, &end);
    if (iterator.position > end.position) {
        return false;
    }

    return true;
}

}
