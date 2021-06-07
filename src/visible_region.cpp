#include "visible_region.hpp"

#include "contents.hpp"
#include "mode.hpp"
#include "movement.hpp"
#include "window.hpp"

namespace mag {

void center_in_window(Window_Unified* window, const Mode& mode, Contents_Iterator iterator) {
    size_t target_rows = window->rows() / 2;
    if (!mode.wrap_long_lines) {
        for (size_t i = 0; i < target_rows; ++i) {
            backward_char(&iterator);
            start_of_line(&iterator);
        }
    } else {
        backward_char(&iterator);

        size_t row = 0;
        size_t col = 0;
        for (; !iterator.at_bob(); iterator.retreat()) {
            if (iterator.get() == '\n') {
                ++row;
                if (row >= target_rows) {
                    start_of_visual_line(window, mode, &iterator);
                    break;
                }
                col = 0;
            } else {
                ++col;
                if (col >= window->cols()) {
                    col -= window->cols();
                    ++row;
                    if (row >= target_rows) {
                        start_of_visual_line(window, mode, &iterator);
                        break;
                    }
                }
            }
        }
    }

    window->start_position = iterator.position;
}

Contents_Iterator center_of_window(Window_Unified* window,
                                   const Mode& mode,
                                   const Contents* contents) {
    Contents_Iterator iterator = contents->iterator_at(window->start_position);
    size_t target_rows = window->rows() / 2;
    if (!mode.wrap_long_lines) {
        for (size_t i = 0; i < target_rows; ++i) {
            end_of_line(&iterator);
            forward_char(&iterator);
        }
    } else {
        size_t row = 0;
        size_t col = 0;
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
                if (col >= window->cols()) {
                    ++row;
                    if (row >= target_rows) {
                        iterator.advance();
                        break;
                    }
                    col -= window->cols();
                }
            }
        }
    }
    return iterator;
}

bool is_visible(const Window_Unified* window, const Mode& mode, Contents_Iterator iterator) {
    if (iterator.position < window->start_position) {
        return false;
    }

    Contents_Iterator end = iterator;
    // Go to start position
    end.retreat_to(window->start_position);
    // Then advance to end of visible region
    forward_visual_line(window, mode, &end, window->rows());
    return iterator.position < end.position;
}

size_t get_scroll_outside(size_t rows, size_t scroll_outside) {
    if (rows < scroll_outside * 2) {
        scroll_outside = rows / 2;
    }
    return scroll_outside;
}

}
