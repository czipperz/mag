#include "alternate.hpp"

#include <cz/heap_string.hpp>
#include <cz/heap_vector.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "movement.hpp"
#include "overlay.hpp"
#include "prose/helpers.hpp"
#include "syntax/overlay_highlight_string.hpp"
#include "token.hpp"

namespace mag {
namespace prose {

static void run_search(Client* client,
                       Editor* editor,
                       const char* directory,
                       cz::Str query,
                       bool query_word) {
    cz::Heap_Vector<cz::Str> args = {};
    CZ_DEFER(args.drop());
    {
        cz::Str defargs[] = {"ag", "--column", "--fixed-strings", "--case-sensitive", "--", query};
        args.reserve(cz::len(defargs) + query_word);
        args.append(defargs);

        if (query_word) {
            args.insert(2, "--word-regexp");
        }
    }

    cz::String buffer_name = {};
    CZ_DEFER(buffer_name.drop(cz::heap_allocator()));
    buffer_name.reserve(cz::heap_allocator(), 3 + query.len);
    buffer_name.append("ag ");
    buffer_name.append(query);

    cz::Arc<Buffer_Handle> handle;
    if (run_console_command(client, editor, directory, args, buffer_name, "Ag error", &handle) ==
        Run_Console_Command_Result::SUCCESS_NEW_BUFFER) {
        Buffer* buffer = handle->lock_writing();
        CZ_DEFER(handle->unlock());
        buffer->mode.overlays.reserve(1);
        buffer->mode.overlays.push(syntax::overlay_highlight_string(
            editor->theme.special_faces[Face_Type::SEARCH_MODE_RESULT_HIGHLIGHT], query,
            Case_Handling::CASE_SENSITIVE, Token_Type::SEARCH_RESULT));
    }
}

static void command_search_in_current_directory_callback(Editor* editor,
                                                         Client* client,
                                                         cz::Str query,
                                                         void*) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(client);
        if (!copy_buffer_directory(editor, client, buffer, &directory)) {
            return;
        }
    }

    run_search(client, editor, directory.buffer(), query, false);
}

void command_search_in_current_directory_prompt(Editor* editor, Command_Source source) {
    cz::String selected_region = {};
    CZ_DEFER(selected_region.drop(cz::heap_allocator()));
    get_selected_region(editor, source.client, cz::heap_allocator(), &selected_region);

    Dialog dialog = {};
    dialog.prompt = "Search in current directory: ";
    dialog.response_callback = command_search_in_current_directory_callback;
    dialog.mini_buffer_contents = selected_region;
    source.client->show_dialog(editor, dialog);
}

static void command_search_in_version_control_callback(Editor* editor,
                                                       Client* client,
                                                       cz::Str query,
                                                       void*) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(client);
        if (!copy_version_control_directory(editor, client, buffer, &directory)) {
            return;
        }
    }

    run_search(client, editor, directory.buffer(), query, false);
}

void command_search_in_version_control_prompt(Editor* editor, Command_Source source) {
    cz::String selected_region = {};
    CZ_DEFER(selected_region.drop(cz::heap_allocator()));
    get_selected_region(editor, source.client, cz::heap_allocator(), &selected_region);

    Dialog dialog = {};
    dialog.prompt = "Search in version control: ";
    dialog.response_callback = command_search_in_version_control_callback;
    dialog.mini_buffer_contents = selected_region;
    source.client->show_dialog(editor, dialog);
}

template <class Copy_Directory>
static void search_token_at_position(Editor* editor,
                                     Client* client,
                                     Copy_Directory&& copy_directory) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    cz::String query = {};
    CZ_DEFER(query.drop(cz::heap_allocator()));

    {
        WITH_SELECTED_BUFFER(client);

        if (!copy_directory(editor, client, buffer, &directory)) {
            return;
        }

        Contents_Iterator iterator =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        Token token;
        if (!get_token_at_position(buffer, &iterator, &token)) {
            client->show_message(editor, "Cursor is not positioned at a token");
            return;
        }

        query.reserve(cz::heap_allocator(), token.end - token.start);
        buffer->contents.slice_into(iterator, token.end, &query);
    }

    run_search(client, editor, directory.buffer(), query, true);
}

void command_search_in_current_directory_token_at_position(Editor* editor, Command_Source source) {
    search_token_at_position(editor, source.client, copy_buffer_directory);
}

void command_search_in_version_control_token_at_position(Editor* editor, Command_Source source) {
    search_token_at_position(editor, source.client, copy_version_control_directory);
}

}
}
