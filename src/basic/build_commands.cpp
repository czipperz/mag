#include "build_commands.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include "basic/search_buffer_commands.hpp"
#include "basic/visible_region_commands.hpp"
#include "core/command_macros.hpp"
#include "core/file.hpp"
#include "core/job.hpp"
#include "core/movement.hpp"
#include "core/token_iterator.hpp"
#include "version_control/version_control.hpp"

namespace mag {
namespace basic {

REGISTER_COMMAND(command_build_debug_vc_root);
void command_build_debug_vc_root(Editor* editor, Command_Source source) {
    cz::String top_level_path = {};
    CZ_DEFER(top_level_path.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!version_control::get_root_directory(buffer->directory.buffer, cz::heap_allocator(),
                                                 &top_level_path)) {
            source.client->show_message("No version control repository found");
            return;
        }
    }

#ifdef _WIN32
    cz::Str args[] = {"powershell", ".\\build-debug"};
#else
    cz::Str args[] = {"/usr/bin/bash", "-c", "LANG=C ./build-debug"};
#endif
    run_console_command(source.client, editor, top_level_path.buffer, args, "build debug");
}

static void open_result(Editor* editor, Client* client, cz::Str path) {
    if (!client->selected_normal_window->parent || !client->selected_normal_window->parent->fused) {
        Window_Split* split = split_window(client, Window::VERTICAL_SPLIT);
        split->fused = true;
    } else {
        toggle_cycle_window(client);
    }

    open_file_arg(editor, client, path);
}

namespace {
enum class Direction {
    CURRENT,
    NEXT,
    PREVIOUS,
};
}

static bool find_path_in_direction(Editor* editor,
                                   Client* client,
                                   Direction direction,
                                   cz::Heap_String* path) {
    WITH_SELECTED_BUFFER(client);

    if (direction != Direction::CURRENT && (window->cursors.len > 1 || window->show_marks)) {
        Contents_Iterator it =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        basic::iterate_cursors(window, buffer, direction == Direction::NEXT, &it);

        basic::center_selected_cursor(editor, window, buffer);
        direction = Direction::CURRENT;
    }

    buffer->token_cache.update(buffer);

    Token token;
    Contents_Iterator iterator;
    if (direction == Direction::NEXT) {
        Forward_Token_Iterator token_iterator;
        token_iterator.init_after(buffer, window->cursors[window->selected_cursor].point);
        if (!token_iterator.find_type(Token_Type::LINK_HREF)) {
            return false;
        }
        token = token_iterator.token();
        iterator = token_iterator.iterator_at_token_start();
    } else {
        Backward_Token_Iterator token_iterator;
        token_iterator.init_at_or_before(buffer, window->cursors[window->selected_cursor].point -
                                                     (direction == Direction::PREVIOUS));
        if (!token_iterator.rfind_type(Token_Type::LINK_HREF)) {
            return false;
        }
        token = token_iterator.token();
        iterator = token_iterator.iterator_at_token_start();
    }

    if (direction != Direction::CURRENT) {
        kill_extra_cursors(window, client);
        window->cursors[window->selected_cursor].point = token.start;
        basic::center_selected_cursor(editor, window, buffer);
    }

    cz::String rel_path = {};
    CZ_DEFER(rel_path.drop(cz::heap_allocator()));
    buffer->contents.slice_into(cz::heap_allocator(), iterator, token.end, &rel_path);

    cz::path::make_absolute(rel_path, buffer->directory, cz::heap_allocator(), path);
    return true;
}

void build_buffer_iterate(Editor* editor, Client* client, bool select_next) {
    cz::Heap_String path = {};
    CZ_DEFER(path.drop());
    if (find_path_in_direction(editor, client, select_next ? Direction::NEXT : Direction::PREVIOUS,
                               &path)) {
        open_result(editor, client, path);
    }
}

REGISTER_COMMAND(command_build_next_link);
void command_build_next_link(Editor* editor, Command_Source source) {
    cz::Heap_String path = {};
    CZ_DEFER(path.drop());
    find_path_in_direction(editor, source.client, Direction::NEXT, &path);
}

REGISTER_COMMAND(command_build_previous_link);
void command_build_previous_link(Editor* editor, Command_Source source) {
    cz::Heap_String path = {};
    CZ_DEFER(path.drop());
    find_path_in_direction(editor, source.client, Direction::PREVIOUS, &path);
}

static void helper(Editor* editor,
                   const Command_Source& source,
                   Direction direction,
                   bool no_swap) {
    cz::Heap_String path = {};
    CZ_DEFER(path.drop());
    if (find_path_in_direction(editor, source.client, direction, &path)) {
        open_result(editor, source.client, path);
        if (no_swap) {
            toggle_cycle_window(source.client);
        }
    }
}
REGISTER_COMMAND(command_build_open_link_at_point);
void command_build_open_link_at_point(Editor* editor, Command_Source source) {
    helper(editor, source, Direction::CURRENT, false);
}
REGISTER_COMMAND(command_build_open_link_at_point_no_swap);
void command_build_open_link_at_point_no_swap(Editor* editor, Command_Source source) {
    helper(editor, source, Direction::CURRENT, true);
}
REGISTER_COMMAND(command_build_open_next_link);
void command_build_open_next_link(Editor* editor, Command_Source source) {
    helper(editor, source, Direction::NEXT, false);
}
REGISTER_COMMAND(command_build_open_next_link_no_swap);
void command_build_open_next_link_no_swap(Editor* editor, Command_Source source) {
    helper(editor, source, Direction::NEXT, true);
}
REGISTER_COMMAND(command_build_open_previous_link);
void command_build_open_previous_link(Editor* editor, Command_Source source) {
    helper(editor, source, Direction::PREVIOUS, false);
}
REGISTER_COMMAND(command_build_open_previous_link_no_swap);
void command_build_open_previous_link_no_swap(Editor* editor, Command_Source source) {
    helper(editor, source, Direction::PREVIOUS, true);
}

////////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_build_next_file);
void command_build_next_file(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    buffer->token_cache.update(buffer);

    Forward_Token_Iterator token_iterator;
    token_iterator.init_after(buffer, window->cursors[window->selected_cursor].point);

    uint64_t position;
    if (token_iterator.find_type(Token_Type::PATCH_COMMIT_CONTEXT)) {
        position = token_iterator.token().start;
    } else {
        position = buffer->contents.len;
    }

    kill_extra_cursors(window, source.client);
    window->cursors[window->selected_cursor].point = position;
    window->start_position = window->cursors[window->selected_cursor].point;
    window->column_offset = 0;
}

REGISTER_COMMAND(command_build_previous_file);
void command_build_previous_file(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    buffer->token_cache.update(buffer);

    Backward_Token_Iterator token_iterator;
    token_iterator.init_at_or_before(
        buffer, std::max(window->cursors[window->selected_cursor].point, (uint64_t)1) - 1);

    uint64_t position;
    if (token_iterator.rfind_type(Token_Type::PATCH_COMMIT_CONTEXT)) {
        position = token_iterator.token().start;
    } else {
        position = 0;
    }

    kill_extra_cursors(window, source.client);
    window->cursors[window->selected_cursor].point = position;
    window->start_position = window->cursors[window->selected_cursor].point;
    window->column_offset = 0;
}

}
}
