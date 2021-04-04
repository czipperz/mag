#include "git.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "face.hpp"
#include "file.hpp"
#include "message.hpp"
#include "movement.hpp"
#include "overlay.hpp"
#include "syntax/overlay_highlight_string.hpp"
#include "token.hpp"

namespace mag {
namespace git {

static void run_git_grep(Client* client,
                         Editor* editor,
                         const char* directory,
                         cz::Str query,
                         cz::Str highlight) {
    cz::Str args[] = {"git", "grep", "-n", "--column", "-e", query, "--", ":/"};

    cz::String buffer_name = {};
    CZ_DEFER(buffer_name.drop(cz::heap_allocator()));
    buffer_name.reserve(cz::heap_allocator(), 9 + query.len);
    buffer_name.append("git grep ");
    buffer_name.append(query);

    cz::Arc<Buffer_Handle> handle;
    if (run_console_command(client, editor, directory, args, buffer_name, "Git grep error",
                            &handle)) {
        Buffer* buffer = handle->lock_writing();
        CZ_DEFER(handle->unlock());
        buffer->mode.overlays.reserve(cz::heap_allocator(), 1);
        buffer->mode.overlays.push(syntax::overlay_highlight_string(editor->theme.faces[12],
                                                                    highlight,
                                                                    /*case_insensitive=*/false));
    }
}

static void command_git_grep_callback(Editor* editor, Client* client, cz::Str query, void* data) {
    cz::String top_level_path = {};
    CZ_DEFER(top_level_path.drop(cz::heap_allocator()));
    {
        WITH_CONST_BUFFER(*(Buffer_Id*)data);
        if (!get_git_top_level(editor, client, buffer->directory.buffer(), cz::heap_allocator(),
                               &top_level_path)) {
            return;
        }
    }

    run_git_grep(client, editor, top_level_path.buffer(), query, query);
}

void command_git_grep(Editor* editor, Command_Source source) {
    Buffer_Id* selected_buffer_id = cz::heap_allocator().alloc<Buffer_Id>();
    CZ_ASSERT(selected_buffer_id);
    *selected_buffer_id = source.client->selected_window()->id;
    source.client->show_dialog(editor, "git grep: ", no_completion_engine,
                               command_git_grep_callback, selected_buffer_id);
    source.client->fill_mini_buffer_with_selected_region(editor);
}

void command_git_grep_token_at_position(Editor* editor, Command_Source source) {
    cz::String top_level_path = {};
    CZ_DEFER(top_level_path.drop(cz::heap_allocator()));
    cz::String query = {};
    CZ_DEFER(query.drop(cz::heap_allocator()));

    {
        WITH_SELECTED_BUFFER(source.client);

        if (!get_git_top_level(editor, source.client, buffer->directory.buffer(),
                               cz::heap_allocator(), &top_level_path)) {
            source.client->show_message(editor, "No git directory");
            return;
        }

        Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);
        Token token;
        if (!get_token_at_position(buffer, &iterator, &token)) {
            source.client->show_message(editor, "Cursor is not positioned at a token");
            return;
        }

        query.reserve(cz::heap_allocator(), token.end - token.start + 4);
        query.push('\\');
        query.push('<');
        buffer->contents.slice_into(iterator, token.end, &query);
        query.push('\\');
        query.push('>');
    }

    run_git_grep(source.client, editor, top_level_path.buffer(), query,
                 query.slice(2, query.len() - 2));
}

}
}
