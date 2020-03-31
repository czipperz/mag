#include "visible_region_commands.hpp"

#include "command_macros.hpp"
#include "movement.hpp"
#include "visible_region.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

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
                //forward_line(&iterator);
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
                    //forward_line(&iterator);
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
    end.retreat(end.position - window->start_position);
    // Then advance to end of visible region
    compute_visible_end(window, &end);
    if (iterator.position > end.position) {
        return false;
    }

    return true;
}

void command_center_in_window(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    center_in_window(window, buffer->contents.iterator_at(window->cursors[0].point));
}

void command_goto_center_of_window(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    window->cursors[0].point = center_of_window(window, &buffer->contents).position;
}

void command_up_page(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    kill_extra_cursors(window, source.client);
    Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);
    compute_visible_start(window, &it);
    window->start_position = it.position;
    window->cursors[0].point = it.position;
}

void command_down_page(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    kill_extra_cursors(window, source.client);
    Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);
    compute_visible_end(window, &it);
    window->start_position = it.position;
    window->cursors[0].point = it.position;
}

}
}
