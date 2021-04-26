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
                         bool word_match) {
    // We want backslashes in the query to be treated as plain text so we need to escape them.
    cz::String query_escaped = {};
    CZ_DEFER(query_escaped.drop(cz::heap_allocator()));
    query_escaped.reserve(cz::heap_allocator(), query.len + query.count('\\') + 4 * word_match);
    if (word_match) {
        query_escaped.append("\\<");
    }
    for (size_t i = 0; i < query.len; ++i) {
        if (query[i] == '\\') {
            query_escaped.push('\\');
        }
        query_escaped.push(query[i]);
    }
    if (word_match) {
        query_escaped.append("\\>");
    }

    cz::Str args[] = {"git", "grep", "-n", "--column", "-e", query_escaped, "--", ":/"};

    cz::String buffer_name = {};
    CZ_DEFER(buffer_name.drop(cz::heap_allocator()));
    buffer_name.reserve(cz::heap_allocator(), 9 + query.len + 4 * word_match);
    buffer_name.append("git grep ");
    if (word_match) {
        buffer_name.append("\\<");
    }
    buffer_name.append(query);
    if (word_match) {
        buffer_name.append("\\>");
    }

    cz::Arc<Buffer_Handle> handle;
    if (run_console_command(client, editor, directory, args, buffer_name, "Git grep error",
                            &handle) == Run_Console_Command_Result::SUCCESS_NEW_BUFFER) {
        Buffer* buffer = handle->lock_writing();
        CZ_DEFER(handle->unlock());
        buffer->mode.overlays.reserve(1);
        buffer->mode.overlays.push(syntax::overlay_highlight_string(
            editor->theme.special_faces[Face_Type::SEARCH_MODE_RESULT_HIGHLIGHT], query,
            /*case_insensitive=*/false, Token_Type::SEARCH_RESULT));
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

    run_git_grep(client, editor, top_level_path.buffer(), query, false);
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

        query.reserve(cz::heap_allocator(), token.end - token.start);
        buffer->contents.slice_into(iterator, token.end, &query);
    }

    run_git_grep(source.client, editor, top_level_path.buffer(), query, true);
}

}
}
