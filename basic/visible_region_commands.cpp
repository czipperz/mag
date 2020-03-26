#include "visible_region_commands.hpp"

#include "command_macros.hpp"

namespace mag {
namespace basic {

void center_in_window(Window_Unified* window, Contents_Iterator iterator) {
    if (iterator.at_eob() && !iterator.at_bob()) {
        iterator.retreat();
    }

    size_t traversed_rows = 0;
    size_t target_rows = window->rows / 2;
    while (1) {
        if (iterator.at_bob() || traversed_rows > target_rows) {
            window->start_position = iterator.position;
            break;
        }

        if (iterator.get() == '\n') {
            ++traversed_rows;
        }
        iterator.retreat();
    }
}

Contents_Iterator center_of_window(Window_Unified* window, const Contents* contents) {
    Contents_Iterator iterator = contents->iterator_at(window->start_position);
    size_t traversed_rows = 0;
    size_t target_rows = window->rows / 2;
    while (1) {
        if (iterator.at_eob() || traversed_rows > target_rows) {
            return iterator;
        }

        if (iterator.get() == '\n') {
            ++traversed_rows;
        }
        iterator.advance();
    }
}

void command_center_in_window(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    center_in_window(window, buffer->contents.iterator_at(window->cursors[0].point));
}

void command_goto_center_of_window(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    window->cursors[0].point = center_of_window(window, &buffer->contents).position;
}

}
}
