#include "search_commands.hpp"

#include "command_macros.hpp"
#include "movement.hpp"

namespace mag {
namespace custom {

void command_search_open(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        Contents_Iterator base_end = buffer->contents.iterator_at(0);
        while (1) {
            if (base_end.at_eob()) {
                return;
            }
            if (base_end.get() == ':') {
                break;
            }
            base_end.advance();
        }

        Contents_Iterator relative_start = buffer->contents.iterator_at(window->cursors[0].point);
        start_of_line(&relative_start);
        Contents_Iterator relative_end = relative_start;
        while (1) {
            if (relative_end.at_eob()) {
                return;
            }
            if (relative_end.get() == ':') {
                break;
            }
            relative_end.advance();
        }

        Contents_Iterator line_start = relative_end;
        line_start.advance();
        Contents_Iterator line_end = line_start;
        while (1) {
            if (line_end.at_eob()) {
                return;
            }
            if (line_end.get() == ':') {
                break;
            }
            line_end.advance();
        }

        Contents_Iterator column_start = line_end;
        column_start.advance();
        Contents_Iterator column_end = column_start;
        while (1) {
            if (column_end.at_eob()) {
                return;
            }
            if (column_end.get() == ':') {
                break;
            }
            column_end.advance();
        }
    });
}

}
}
