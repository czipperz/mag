#include "visible_region.hpp"

#include "core/contents.hpp"
#include "core/mode.hpp"
#include "core/movement.hpp"
#include "core/theme.hpp"
#include "core/window.hpp"

namespace mag {

void vertically_center_in_window(Window_Unified* window,
                                 const Mode& mode,
                                 const Theme& theme,
                                 Contents_Iterator iterator) {
    size_t window_cols = window->total_cols - line_number_cols(theme, window, iterator.contents);

    size_t target_rows = window->rows() / 2;
    if (!mode.wrap_long_lines) {
        start_of_line(&iterator);
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
                    start_of_visual_line(window, mode, theme, &iterator);
                    break;
                }
                col = 0;
            } else {
                ++col;
                if (col >= window_cols) {
                    col -= window_cols;
                    ++row;
                    if (row >= target_rows) {
                        start_of_visual_line(window, mode, theme, &iterator);
                        break;
                    }
                }
            }
        }
    }

    window->start_position = iterator.position;
}

void horizontally_center_in_window(Window_Unified* window,
                                   const Mode& mode,
                                   const Theme& theme,
                                   Contents_Iterator iterator) {
    if (mode.wrap_long_lines) {
        window->column_offset = 0;
        return;
    }

    size_t window_cols = window->total_cols - line_number_cols(theme, window, iterator.contents);

    Contents_Iterator eol = iterator;
    end_of_line(&eol);

    uint64_t column = get_visual_column(mode, iterator);
    uint64_t line_columns = count_visual_columns(mode, iterator, eol.position, column);
    size_t scroll_outside = get_scroll_outside(window_cols, theme.scroll_outside_visual_columns);

    // If we can either fit within the scroll boundary or the entire
    // line can fit in one screen then just render at a 0 offset.
    if (column + scroll_outside < window_cols || line_columns < window_cols) {
        window->column_offset = 0;
        return;
    }

    // Otherwise, center the iterator.
    window->column_offset = column - window_cols / 2;
}

void center_in_window(Window_Unified* window,
                      const Mode& mode,
                      const Theme& theme,
                      Contents_Iterator iterator) {
    vertically_center_in_window(window, mode, theme, iterator);
    horizontally_center_in_window(window, mode, theme, iterator);
}

Contents_Iterator center_of_window(Window_Unified* window,
                                   const Mode& mode,
                                   const Theme& theme,
                                   const Contents* contents) {
    size_t window_cols = window->total_cols - line_number_cols(theme, window, contents);

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
                if (col >= window_cols) {
                    ++row;
                    if (row >= target_rows) {
                        iterator.advance();
                        break;
                    }
                    col -= window_cols;
                }
            }
        }
    }
    return iterator;
}

Contents_Iterator top_of_window(Window_Unified* window,
                                const Mode& mode,
                                const Theme& theme,
                                const Contents* contents) {
    Contents_Iterator it = contents->iterator_at(window->start_position);
    uint64_t scroll_outside = get_scroll_outside(window->rows(), theme.scroll_outside_visual_rows);
    forward_visual_line(window, mode, theme, &it, scroll_outside);
    return it;
}

Contents_Iterator bottom_of_window(Window_Unified* window,
                                   const Mode& mode,
                                   const Theme& theme,
                                   const Contents* contents) {
    Contents_Iterator it = contents->iterator_at(window->start_position);
    uint64_t scroll_outside = get_scroll_outside(window->rows(), theme.scroll_outside_visual_rows);
    forward_visual_line(window, mode, theme, &it, window->rows() - scroll_outside);
    return it;
}

bool is_visible(const Window_Unified* window,
                const Mode& mode,
                const Theme& theme,
                Contents_Iterator iterator) {
    if (iterator.position < window->start_position) {
        return false;
    }

    Contents_Iterator end = iterator;
    // Go to start position
    end.retreat_to(window->start_position);
    // Then advance to end of visible region
    forward_visual_line(window, mode, theme, &end, window->rows() - 1);
    end_of_visual_line(window, mode, theme, &end);
    return iterator.position <= end.position;
}

size_t get_scroll_outside(size_t rows, size_t scroll_outside) {
    if (rows < scroll_outside * 2 + 1) {
        scroll_outside = (rows - 1) / 2;
    }
    return scroll_outside;
}

}
