#include "search.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/heap_vector.hpp>
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
#include "version_control.hpp"

namespace mag {
namespace version_control {

static void run_search(Client* client,
                       Editor* editor,
                       const char* directory,
                       cz::Str query,
                       bool word_match) {
    cz::Heap_Vector<cz::Str> args = {};
    CZ_DEFER(args.drop());
    {
        cz::Str defargs[] = {
            "git", "grep", "--line-number", "--column", "--fixed-strings", "-e", query, "--", ":/"};
        args.reserve(cz::len(defargs) + word_match);
        args.append(defargs);

        if (word_match) {
            args.insert(4, "--word-regexp");
        }
    }

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

static void command_search_callback(Editor* editor, Client* client, cz::Str query, void* data) {
    cz::String top_level_path = {};
    CZ_DEFER(top_level_path.drop(cz::heap_allocator()));
    {
        WITH_CONST_BUFFER(*(Buffer_Id*)data);
        if (!get_root_directory(editor, client, buffer->directory.buffer(), cz::heap_allocator(),
                                &top_level_path)) {
            return;
        }
    }

    run_search(client, editor, top_level_path.buffer(), query, false);
}

void command_search(Editor* editor, Command_Source source) {
    Buffer_Id* selected_buffer_id = cz::heap_allocator().alloc<Buffer_Id>();
    CZ_ASSERT(selected_buffer_id);
    *selected_buffer_id = source.client->selected_window()->id;
    source.client->show_dialog(editor, "git grep: ", no_completion_engine, command_search_callback,
                               selected_buffer_id);
    source.client->fill_mini_buffer_with_selected_region(editor);
}

void command_search_token_at_position(Editor* editor, Command_Source source) {
    cz::String top_level_path = {};
    CZ_DEFER(top_level_path.drop(cz::heap_allocator()));
    cz::String query = {};
    CZ_DEFER(query.drop(cz::heap_allocator()));

    {
        WITH_SELECTED_BUFFER(source.client);

        if (!get_root_directory(editor, source.client, buffer->directory.buffer(),
                                cz::heap_allocator(), &top_level_path)) {
            source.client->show_message(editor, "No git directory");
            return;
        }

        Contents_Iterator iterator =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        Token token;
        if (!get_token_at_position(buffer, &iterator, &token)) {
            source.client->show_message(editor, "Cursor is not positioned at a token");
            return;
        }

        query.reserve(cz::heap_allocator(), token.end - token.start);
        buffer->contents.slice_into(iterator, token.end, &query);
    }

    run_search(source.client, editor, top_level_path.buffer(), query, true);
}

}
}
