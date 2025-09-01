#include "build_commands.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
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

static bool find_path_in_direction(Editor* editor,
                                   Client* client,
                                   bool select_next,
                                   bool move_cursor,
                                   cz::Heap_String* path) {
    WITH_SELECTED_BUFFER(client);
    buffer->token_cache.update(buffer);

    Token token;
    Contents_Iterator iterator;
    if (select_next) {
        Forward_Token_Iterator token_iterator;
        token_iterator.init_after(buffer, window->cursors[window->selected_cursor].point + 1);
        if (!token_iterator.find_type(Token_Type::LINK_HREF)) {
            return false;
        }
        token = token_iterator.token();
        iterator = token_iterator.iterator_at_token_start();
    } else {
        Backward_Token_Iterator token_iterator;
        token_iterator.init_at_or_before(buffer,
                                         window->cursors[window->selected_cursor].point - 1);
        if (!token_iterator.rfind_type(Token_Type::LINK_HREF)) {
            return false;
        }
        token = token_iterator.token();
        iterator = token_iterator.iterator_at_token_start();
    }

    if (move_cursor) {
        kill_extra_cursors(window, client);
        window->cursors[window->selected_cursor].point = token.start;
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
    if (find_path_in_direction(editor, client, select_next, true, &path)) {
        open_result(editor, client, path);
    }
}

static void helper(Editor* editor,
                   const Command_Source& source,
                   bool select_next,
                   bool move_cursor,
                   bool no_swap) {
    cz::Heap_String path = {};
    CZ_DEFER(path.drop());
    if (find_path_in_direction(editor, source.client, select_next, move_cursor, &path)) {
        open_result(editor, source.client, path);
        if (no_swap) {
            toggle_cycle_window(source.client);
        }
    }
}
REGISTER_COMMAND(command_build_open_link_at_point);
void command_build_open_link_at_point(Editor* editor, Command_Source source) {
    helper(editor, source, false, false, false);
}
REGISTER_COMMAND(command_build_open_link_at_point_no_swap);
void command_build_open_link_at_point_no_swap(Editor* editor, Command_Source source) {
    helper(editor, source, false, false, true);
}
REGISTER_COMMAND(command_build_open_next_link);
void command_build_open_next_link(Editor* editor, Command_Source source) {
    helper(editor, source, true, true, false);
}
REGISTER_COMMAND(command_build_open_next_link_no_swap);
void command_build_open_next_link_no_swap(Editor* editor, Command_Source source) {
    helper(editor, source, true, true, true);
}
REGISTER_COMMAND(command_build_open_previous_link);
void command_build_open_previous_link(Editor* editor, Command_Source source) {
    helper(editor, source, false, true, false);
}
REGISTER_COMMAND(command_build_open_previous_link_no_swap);
void command_build_open_previous_link_no_swap(Editor* editor, Command_Source source) {
    helper(editor, source, false, true, true);
}

}
}
