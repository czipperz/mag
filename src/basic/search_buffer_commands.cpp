#include "search_buffer_commands.hpp"

#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/path.hpp>
#include <cz/util.hpp>
#include <syntax/tokenize_search.hpp>
#include "core/command_macros.hpp"
#include "core/file.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/visible_region.hpp"
#include "search_commands.hpp"
#include "window_commands.hpp"

namespace mag {
namespace basic {

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

    // Ignore return value so that if the user does like 'find' we can still
    // handle the output by just treating the entire line as a relpath.
    (void)find_this_line(&relative_end, ':');

    Contents_Iterator iterator = relative_end;
    if (!at_end_of_line(iterator)) {
        if (!parse_number(&iterator, line)) {
            return false;
        }
        if (!parse_number(&iterator, column)) {
            *column = 0;
        }
    }

    if (relative_end.position <= relative_start.position)
        return false;

    path->reserve(cz::heap_allocator(),
                  buffer->directory.len + relative_end.position - relative_start.position);
    path->append(buffer->directory);
    buffer->contents.slice_into(relative_start, relative_end.position, path);
    if (cz::path::is_absolute(path->slice_start(buffer->directory.len))) {
        path->remove_range(0, buffer->directory.len);
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

    open_file_at(editor, client, path, line, column);

    toggle_cycle_window(client);
}

REGISTER_COMMAND(command_search_buffer_reload);
void command_search_buffer_reload(Editor* editor, Command_Source source) {
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
        buffer->token_cache.reset(buffer);
        buffer->contents.remove(end.position, buffer->contents.len - end.position);
    }

    kill_extra_cursors(window, source.client);
    window->cursors[0].point = window->cursors[0].mark = buffer->contents.len;
    window->show_marks = false;

    run_console_command_in(source.client, editor, handle, buffer->directory.buffer,
                           script.as_str());
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

REGISTER_COMMAND(command_search_buffer_open_selected_no_swap);
void command_search_buffer_open_selected_no_swap(Editor* editor, Command_Source source) {
    search_open_selected_no_swap(editor, source.client);
}

REGISTER_COMMAND(command_search_buffer_open_selected);
void command_search_buffer_open_selected(Editor* editor, Command_Source source) {
    search_open_selected_no_swap(editor, source.client);
    toggle_cycle_window(source.client);
}

bool iterate_cursors(Window_Unified* window,
                     const Buffer* buffer,
                     bool select_next,
                     Contents_Iterator* it) {
    if (window->cursors.len > 1) {
        // In multi-cursor mode, go to the result at the next/previous cursor.
        if (select_next) {
            if (window->selected_cursor + 1 >= window->cursors.len)
                return false;
            ++window->selected_cursor;
        } else {
            if (window->selected_cursor == 0)
                return false;
            --window->selected_cursor;
        }
        it->go_to(window->cursors[window->selected_cursor].point);
    } else if (window->show_marks) {
        // Go to the result at the next/previous matching region.
        Cursor& cursor = window->cursors[window->selected_cursor];
        it->retreat_to(cursor.start());
        if (select_next ? !search_forward_slice(buffer, it, cursor.end())
                        : !search_backward_slice(buffer, it, cursor.end()))
            return false;

        uint64_t selection_len = (cursor.end() - cursor.start());
        if (cursor.point == cursor.start()) {
            cursor.point = it->position;
            cursor.mark = it->position + selection_len;
        } else {
            cursor.point = it->position + selection_len;
            cursor.mark = it->position;
        }
    } else {
        CZ_PANIC("iterate_cursors can only be called with multiple cursors or a selected region");
    }
    return true;
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
            client->show_message("Error: trying to parse buffer not in search mode");
            return;
        }

        Contents_Iterator it =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);

        if (window->cursors.len > 1 || window->show_marks) {
            if (!iterate_cursors(window, buffer, /*select_next=*/true, &it))
                return;
        } else {
            // Default case, just go to result on next line.
            forward_line(buffer->mode, &it);
            window->cursors[window->selected_cursor].point = it.position;
        }

        found = get_file_to_open(buffer, it, &path, &line, &column);
    }
    if (!found) {
        return;
    }

    open_file_and_goto_position(editor, client, path, line, column);
}

REGISTER_COMMAND(command_search_buffer_open_next_no_swap);
void command_search_buffer_open_next_no_swap(Editor* editor, Command_Source source) {
    search_open_next_no_swap(editor, source.client);
}

REGISTER_COMMAND(command_search_buffer_open_next);
void command_search_buffer_open_next(Editor* editor, Command_Source source) {
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
            client->show_message("Error: trying to parse buffer not in search mode");
            return;
        }

        Contents_Iterator it =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        if (window->cursors.len > 1 || window->show_marks) {
            if (!iterate_cursors(window, buffer, /*select_next=*/false, &it))
                return;
        } else {
            // Default case, just go to result on previous line.
            backward_line(buffer->mode, &it);
            window->cursors[window->selected_cursor].point = it.position;
        }

        found = get_file_to_open(buffer, it, &path, &line, &column);
    }
    if (!found) {
        return;
    }

    open_file_and_goto_position(editor, client, path, line, column);
}

REGISTER_COMMAND(command_search_buffer_open_previous_no_swap);
void command_search_buffer_open_previous_no_swap(Editor* editor, Command_Source source) {
    search_open_previous_no_swap(editor, source.client);
}

REGISTER_COMMAND(command_search_buffer_open_previous);
void command_search_buffer_open_previous(Editor* editor, Command_Source source) {
    search_open_previous_no_swap(editor, source.client);
    toggle_cycle_window(source.client);
}

void search_buffer_iterate(Editor* editor, Client* client, bool select_next) {
    if (select_next)
        search_open_next_no_swap(editor, client);
    else
        search_open_previous_no_swap(editor, client);
    toggle_cycle_window(client);
}

}
}
