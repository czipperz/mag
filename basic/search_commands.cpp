#include "search_commands.hpp"

#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/path.hpp>
#include <cz/util.hpp>
#include <syntax/tokenize_search.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "visible_region.hpp"
#include "window_commands.hpp"

namespace mag {
namespace basic {

static bool eat_until_colon(Contents_Iterator* iterator) {
    return find(iterator, ':');
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
        if (!cz::is_digit(ch)) {
            return false;
        }
        *num *= 10;
        *num += ch - '0';
        iterator->advance();
    }
}

static bool get_file_to_open(const Buffer* buffer,
                             Contents_Iterator relative_start,
                             cz::String* path,
                             uint64_t* line,
                             uint64_t* column) {
    start_of_line(&relative_start);
    if (relative_start.position == 0) {
        return false;
    }

    Contents_Iterator relative_end = relative_start;
    if (!eat_until_colon(&relative_end)) {
        return false;
    }

    Contents_Iterator iterator = relative_end;
    if (!parse_number(&iterator, line)) {
        return false;
    }
    if (!parse_number(&iterator, column)) {
        *column = 0;
    }

    path->reserve(cz::heap_allocator(),
                  buffer->directory.len() + relative_end.position - relative_start.position);
    path->append(buffer->directory);
    buffer->contents.slice_into(relative_start, relative_end.position, path);
    if (cz::path::is_absolute(path->slice_start(buffer->directory.len()))) {
        path->remove_range(0, buffer->directory.len());
    }
    return true;
}

static void open_file_and_goto_position(Editor* editor,
                                        Client* client,
                                        cz::Str path,
                                        uint64_t line,
                                        uint64_t column) {
    if (!client->selected_normal_window->parent || !client->selected_normal_window->parent->fused) {
        Window_Split* split = split_window(client, Window::HORIZONTAL_SPLIT);
        split->split_ratio = 0.75f;
        split->fused = true;
    }

    toggle_cycle_window(client);

    open_file(editor, client, path);

    {
        WITH_CONST_SELECTED_BUFFER(client);
        kill_extra_cursors(window, client);
        Contents_Iterator iterator = iterator_at_line_column(buffer->contents, line, column);
        window->cursors[0].point = iterator.position;
        center_in_window(window, buffer->mode, editor->theme, iterator);
    }

    toggle_cycle_window(client);
}

void command_search_reload(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    Contents_Iterator start = buffer->contents.start();
    Contents_Iterator end = start;
    end_of_line(&end);

    SSOStr script = buffer->contents.slice(cz::heap_allocator(), start, end.position);
    CZ_DEFER(script.drop(cz::heap_allocator()));

    if (end.at_eob()) {
        // There is no newline so insert one.
        buffer->contents.append("\n");
    } else {
        // Skip over the newline and delete to the end of the file.
        end.advance();
        buffer->token_cache.reset();
        buffer->contents.remove(end.position, buffer->contents.len - end.position);
    }

    kill_extra_cursors(window, source.client);
    window->cursors[0].point = window->cursors[0].mark = buffer->contents.len;

    run_console_command_in(source.client, editor, handle, buffer->directory.buffer(),
                           script.as_str(), "Failed to rerun script");
}

static void search_open_selected_no_swap(Editor* editor, Client* client) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    uint64_t line = 0;
    uint64_t column = 0;

    bool found;
    {
        WITH_CONST_SELECTED_BUFFER(client);
        found = get_file_to_open(
            buffer, buffer->contents.iterator_at(window->cursors[window->selected_cursor].point),
            &path, &line, &column);
    }
    if (!found) {
        return;
    }

    open_file_and_goto_position(editor, client, path, line, column);
}

void command_search_open_selected_no_swap(Editor* editor, Command_Source source) {
    search_open_selected_no_swap(editor, source.client);
}

void command_search_open_selected(Editor* editor, Command_Source source) {
    search_open_selected_no_swap(editor, source.client);
    toggle_cycle_window(source.client);
}

void command_search_continue_selected(Editor* editor, Command_Source source) {
    toggle_cycle_window(source.client);
    search_open_selected_no_swap(editor, source.client);
    toggle_cycle_window(source.client);
}

static void search_open_next_no_swap(Editor* editor, Client* client) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    uint64_t line = 0;
    uint64_t column = 0;

    bool found;
    {
        WITH_CONST_SELECTED_BUFFER(client);
        if (buffer->mode.next_token != syntax::search_next_token) {
            client->show_message(editor, "Error: trying to parse buffer not in search mode");
            return;
        }

        Contents_Iterator it =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        forward_line(buffer->mode, &it);
        window->cursors[window->selected_cursor].point = it.position;

        found = get_file_to_open(buffer, it, &path, &line, &column);
    }
    if (!found) {
        return;
    }

    open_file_and_goto_position(editor, client, path, line, column);
}

void command_search_open_next_no_swap(Editor* editor, Command_Source source) {
    search_open_next_no_swap(editor, source.client);
}

void command_search_open_next(Editor* editor, Command_Source source) {
    search_open_next_no_swap(editor, source.client);
    toggle_cycle_window(source.client);
}

void command_search_continue_next(Editor* editor, Command_Source source) {
    toggle_cycle_window(source.client);
    search_open_next_no_swap(editor, source.client);
    toggle_cycle_window(source.client);
}

static void search_open_previous_no_swap(Editor* editor, Client* client) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    uint64_t line = 0;
    uint64_t column = 0;

    bool found;
    {
        WITH_CONST_SELECTED_BUFFER(client);
        if (buffer->mode.next_token != syntax::search_next_token) {
            client->show_message(editor, "Error: trying to parse buffer not in search mode");
            return;
        }

        Contents_Iterator it =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        backward_line(buffer->mode, &it);
        window->cursors[window->selected_cursor].point = it.position;

        found = get_file_to_open(buffer, it, &path, &line, &column);
    }
    if (!found) {
        return;
    }

    open_file_and_goto_position(editor, client, path, line, column);
}

void command_search_open_previous_no_swap(Editor* editor, Command_Source source) {
    search_open_previous_no_swap(editor, source.client);
}

void command_search_open_previous(Editor* editor, Command_Source source) {
    search_open_previous_no_swap(editor, source.client);
    toggle_cycle_window(source.client);
}

void command_search_continue_previous(Editor* editor, Command_Source source) {
    toggle_cycle_window(source.client);
    search_open_previous_no_swap(editor, source.client);
    toggle_cycle_window(source.client);
}

}
}
