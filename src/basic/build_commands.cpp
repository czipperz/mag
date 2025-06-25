#include "build_commands.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include "core/command_macros.hpp"
#include "core/file.hpp"
#include "core/job.hpp"
#include "core/movement.hpp"
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
    cz::Str args[] = {"./build-debug"};
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
        Tokenizer_Check_Point check_point = {};
        buffer->token_cache.find_check_point(window->cursors[window->selected_cursor].point,
                                             &check_point);
        Contents_Iterator iterator = buffer->contents.iterator_at(check_point.position);
        Token token;
        if (select_next) {
            while (1) {
                if (!buffer->mode.next_token(&iterator, &token, &check_point.state))
                    return;
                if (token.start > window->cursors[window->selected_cursor].point &&
                    token.type == Token_Type::LINK_HREF) {
                    break;
                }
            }
        } else {
            // TODO
            return;
        }

        kill_extra_cursors(window, client);
        window->cursors[window->selected_cursor].point = token.start;

        iterator.retreat_to(token.start);
        buffer->contents.slice_into(cz::heap_allocator(), iterator, token.end, &path);
    }

    open_result(editor, client, path);
}

REGISTER_COMMAND(command_build_open_link_at_point);
void command_build_open_link_at_point(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    {
        WITH_SELECTED_BUFFER(source.client);
        Contents_Iterator iterator =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        Token token;
        if (!get_token_at_position(buffer, &iterator, &token))
            return;
        buffer->contents.slice_into(cz::heap_allocator(), iterator, token.end, &path);
    }

    open_result(editor, source.client, path);
}

}
}
