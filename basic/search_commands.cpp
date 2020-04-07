#include "search_commands.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/util.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "movement.hpp"

namespace mag {
namespace basic {

static bool eat_until_colon(Contents_Iterator* iterator) {
    while (1) {
        if (iterator->at_eob()) {
            return false;
        }
        if (iterator->get() == ':') {
            return true;
        }
        iterator->advance();
    }
}

static bool parse_number(Contents_Iterator* iterator, uint64_t* num) {
    iterator->advance();
    while (1) {
        if (iterator->at_eob()) {
            return false;
        }
        char ch = iterator->get();
        if (ch == ':') {
            return true;
        }
        if (!isdigit(ch)) {
            return false;
        }
        *num *= 10;
        *num += ch - '0';
        iterator->advance();
    }
}

void command_search_open(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    uint64_t line = 0;
    uint64_t column = 0;

    {
        WITH_SELECTED_BUFFER(source.client);
        Contents_Iterator relative_start = buffer->contents.iterator_at(window->cursors[0].point);
        start_of_line(&relative_start);
        Contents_Iterator relative_end = relative_start;
        if (!eat_until_colon(&relative_end)) {
            return;
        }

        Contents_Iterator iterator = relative_end;
        if (!parse_number(&iterator, &line)) {
            return;
        }
        if (!parse_number(&iterator, &column)) {
            return;
        }

        Contents_Iterator base_end = buffer->contents.start();
        if (!eat_until_colon(&base_end)) {
            return;
        }

        path.reserve(cz::heap_allocator(),
                     base_end.position + 1 + relative_end.position - relative_start.position);
        buffer->contents.slice_into(buffer->contents.start(), base_end.position, path.end());
        path.set_len(path.len() + base_end.position);
        path.push('/');
        buffer->contents.slice_into(relative_start, relative_end.position, path.end());
        path.set_len(path.len() + relative_end.position - relative_start.position);
    };

    open_file(editor, source.client, path);

    {
        WITH_SELECTED_BUFFER(source.client);
        push_jump(window, source.client, handle->id, buffer);

        Contents_Iterator iterator = buffer->contents.start();
        while (!iterator.at_eob() && line > 1) {
            if (iterator.get() == '\n') {
                --line;
            }
            iterator.advance();
        }

        window->cursors[0].point = cz::min(buffer->contents.len, iterator.position + column - 1);
    }
}

}
}
