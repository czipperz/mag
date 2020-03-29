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

}
