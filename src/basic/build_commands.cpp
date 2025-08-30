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

void build_buffer_iterate(Editor* editor, Client* client, bool select_next) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    {
        WITH_SELECTED_BUFFER(client);
        buffer->token_cache.update(buffer);
        Forward_Token_Iterator token_iterator;
        token_iterator.init_at_or_after(buffer, window->cursors[window->selected_cursor].point);
        if (select_next) {
            token_iterator.find_after(window->cursors[window->selected_cursor].point + 1);
            if (!token_iterator.find_type(Token_Type::LINK_HREF)) {
                return;  // No result found.
            }
        } else {
            // TODO
            return;
        }

        kill_extra_cursors(window, client);
        window->cursors[window->selected_cursor].point = token_iterator.token.start;
        token_iterator.iterator.retreat_to(token_iterator.token.start);

        cz::String rel_path = {};
        CZ_DEFER(rel_path.drop(cz::heap_allocator()));
        buffer->contents.slice_into(cz::heap_allocator(), token_iterator.iterator,
                                    token_iterator.token.end, &rel_path);

        cz::path::make_absolute(rel_path, buffer->directory, cz::heap_allocator(), &path);
    }

    open_result(editor, client, path);
}

REGISTER_COMMAND(command_build_open_link_at_point);
void command_build_open_link_at_point(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    {
        SSOStr rel_path = {};
        CZ_DEFER(rel_path.drop(cz::heap_allocator()));

        WITH_SELECTED_BUFFER(source.client);
        if (!get_token_at_position_contents(buffer, window->cursors[window->selected_cursor].point,
                                            &rel_path))
            return;

        cz::path::make_absolute(rel_path.as_str(), buffer->directory, cz::heap_allocator(), &path);
    }

    open_result(editor, source.client, path);
}

}
}
