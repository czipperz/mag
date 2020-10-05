#include "search_commands.hpp"

#include <ctype.h>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/util.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "movement.hpp"
#include "window_commands.hpp"

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

static bool get_file_to_open(Buffer* buffer,
                             Contents_Iterator relative_start,
                             cz::String* path,
                             uint64_t* line,
                             uint64_t* column) {
    start_of_line(&relative_start);
    Contents_Iterator relative_end = relative_start;
    if (!eat_until_colon(&relative_end)) {
        return false;
    }

    Contents_Iterator iterator = relative_end;
    if (!parse_number(&iterator, line)) {
        return false;
    }
    if (!parse_number(&iterator, column)) {
        return false;
    }

    Contents_Iterator base_end = buffer->contents.start();
    if (!eat_until_colon(&base_end)) {
        return false;
    }

    path->reserve(cz::heap_allocator(),
                  base_end.position + 1 + relative_end.position - relative_start.position);
    buffer->contents.slice_into(buffer->contents.start(), base_end.position, path->end());
    path->set_len(path->len() + base_end.position);
    path->push('/');
    buffer->contents.slice_into(relative_start, relative_end.position, path->end());
    path->set_len(path->len() + relative_end.position - relative_start.position);
    return true;
}

static void open_file_and_goto_position(Editor* editor,
                                        Client* client,
                                        cz::Str path,
                                        uint64_t line,
                                        uint64_t column) {
    if (client->window == client->selected_normal_window) {
        Window_Unified* window = client->selected_normal_window;
        Window::Tag tag = Window::VERTICAL_SPLIT;
        if (window->rows < 160) {
            tag = Window::HORIZONTAL_SPLIT;
        }
        split_window(editor, client, tag);
    }

    cycle_window(client);

    open_file(editor, client, path);

    WITH_SELECTED_BUFFER(client);
    push_jump(window, client, handle->id, buffer);

    Contents_Iterator iterator = buffer->contents.start();
    while (!iterator.at_eob() && line > 1) {
        if (iterator.get() == '\n') {
            --line;
        }
        iterator.advance();
    }

    window->cursors[0].point = cz::min(buffer->contents.len, iterator.position + column - 1);
}

void command_search_open(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    uint64_t line = 0;
    uint64_t column = 0;

    bool found;
    {
        WITH_SELECTED_BUFFER(source.client);
        found = get_file_to_open(buffer, buffer->contents.iterator_at(window->cursors[0].point),
                                 &path, &line, &column);
    }
    if (!found) {
        return;
    }

    open_file_and_goto_position(editor, source.client, path, line, column);
}

void command_search_open_next(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    uint64_t line = 0;
    uint64_t column = 0;

    reverse_cycle_window(source.client);

    bool found;
    {
        WITH_SELECTED_BUFFER(source.client);
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);
        forward_line(&it);
        window->cursors[0].point = it.position;

        found = get_file_to_open(buffer, it, &path, &line, &column);
    }
    if (!found) {
        cycle_window(source.client);
        return;
    }

    open_file_and_goto_position(editor, source.client, path, line, column);
}

void command_search_open_previous(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    uint64_t line = 0;
    uint64_t column = 0;

    reverse_cycle_window(source.client);

    bool found;
    {
        WITH_SELECTED_BUFFER(source.client);
        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);
        backward_line(&it);
        window->cursors[0].point = it.position;

        found = get_file_to_open(buffer, it, &path, &line, &column);
    }
    if (!found) {
        cycle_window(source.client);
        return;
    }

    open_file_and_goto_position(editor, source.client, path, line, column);
}

}
}
