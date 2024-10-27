#include "search.hpp"

#include <stdio.h>
#include <cz/file.hpp>
#include <cz/heap_string.hpp>
#include <cz/heap_vector.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "movement.hpp"
#include "overlay.hpp"
#include "overlays/overlay_highlight_string.hpp"
#include "prose/helpers.hpp"
#include "token.hpp"

namespace mag {
namespace prose {

static char empty_file_path[L_tmpnam];

static void run_search(Client* client,
                       Editor* editor,
                       const char* directory,
                       cz::Str query,
                       bool query_word,
                       const cz::String* file = nullptr) {
    cz::Heap_Vector<cz::Str> args = {};
    CZ_DEFER(args.drop());
    {
        cz::Str defargs[] = {"ag", "--hidden", "--column", "--fixed-strings", "--case-sensitive",
                             "--", query};
        args.reserve(cz::len(defargs) + query_word + (file ? 2 : 0));
        args.append(defargs);

        if (query_word) {
            args.insert(2, "--word-regexp");
        }

        if (file) {
            args.push(*file);
            args.push(empty_file_path);
        }
    }

    cz::String buffer_name = {};
    CZ_DEFER(buffer_name.drop(cz::heap_allocator()));
    buffer_name.reserve(cz::heap_allocator(), 3 + query.len + (file ? file->len + 1 : 0));
    buffer_name.append("ag ");
    if (file) {
        buffer_name.append(*file);
        buffer_name.push(' ');
    }
    buffer_name.append(query);

    client->close_fused_paired_windows();

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

template <bool copy_directory(Client*, const Buffer*, cz::String*), bool query_word>
static void command_search_in_x_callback(Editor* editor, Client* client, cz::Str query, void*) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(client);
        if (!copy_directory(client, buffer, &directory)) {
            return;
        }
    }

    run_search(client, editor, directory.buffer, query, query_word);
}

REGISTER_COMMAND(command_search_in_current_directory_prompt);
void command_search_in_current_directory_prompt(Editor* editor, Command_Source source) {
    cz::String selected_region = {};
    CZ_DEFER(selected_region.drop(cz::heap_allocator()));
    get_selected_region(editor, source.client, cz::heap_allocator(), &selected_region);

    Dialog dialog = {};
    dialog.prompt = "Search in current directory: ";
    dialog.response_callback = command_search_in_x_callback<copy_buffer_directory, false>;
    dialog.mini_buffer_contents = selected_region;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_search_in_current_directory_word_prompt);
void command_search_in_current_directory_word_prompt(Editor* editor, Command_Source source) {
    cz::String selected_region = {};
    CZ_DEFER(selected_region.drop(cz::heap_allocator()));
    get_selected_region(editor, source.client, cz::heap_allocator(), &selected_region);

    Dialog dialog = {};
    dialog.prompt = "Search in current directory word: ";
    dialog.response_callback = command_search_in_x_callback<copy_buffer_directory, true>;
    dialog.mini_buffer_contents = selected_region;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_search_in_version_control_prompt);
void command_search_in_version_control_prompt(Editor* editor, Command_Source source) {
    cz::String selected_region = {};
    CZ_DEFER(selected_region.drop(cz::heap_allocator()));
    get_selected_region(editor, source.client, cz::heap_allocator(), &selected_region);

    Dialog dialog = {};
    dialog.prompt = "Search in version control: ";
    dialog.response_callback = command_search_in_x_callback<copy_version_control_directory, false>;
    dialog.mini_buffer_contents = selected_region;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_search_in_version_control_word_prompt);
void command_search_in_version_control_word_prompt(Editor* editor, Command_Source source) {
    cz::String selected_region = {};
    CZ_DEFER(selected_region.drop(cz::heap_allocator()));
    get_selected_region(editor, source.client, cz::heap_allocator(), &selected_region);

    Dialog dialog = {};
    dialog.prompt = "Search in version control word: ";
    dialog.response_callback = command_search_in_x_callback<copy_version_control_directory, true>;
    dialog.mini_buffer_contents = selected_region;
    source.client->show_dialog(dialog);
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

        if (!copy_directory(client, buffer, &directory)) {
            return;
        }

        Contents_Iterator iterator =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        Token token;
        if (!get_token_at_position(buffer, &iterator, &token)) {
            client->show_message("Cursor is not positioned at a token");
            return;
        }

        query.reserve(cz::heap_allocator(), token.end - token.start);
        buffer->contents.slice_into(iterator, token.end, &query);
    }

    run_search(client, editor, directory.buffer, query, true);
}

REGISTER_COMMAND(command_search_in_current_directory_token_at_position);
void command_search_in_current_directory_token_at_position(Editor* editor, Command_Source source) {
    search_token_at_position(editor, source.client, copy_buffer_directory);
}

REGISTER_COMMAND(command_search_in_version_control_token_at_position);
void command_search_in_version_control_token_at_position(Editor* editor, Command_Source source) {
    search_token_at_position(editor, source.client, copy_version_control_directory);
}

////////////////////////////////////////////////////////////////////////////////
// command_search_in_file_*
////////////////////////////////////////////////////////////////////////////////

static void ensure_has_empty_file() {
    if (!empty_file_path[0]) {
        if (!tmpnam(empty_file_path)) {
            return;
        }

        // Create the file.
        cz::Output_File file;
        if (file.open(empty_file_path))
            file.close();
    }
}

template <bool query_word>
static void command_search_in_file_callback(Editor* editor, Client* client, cz::Str query, void*) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    cz::String name = {};
    CZ_DEFER(name.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(client);
        if (buffer->type != Buffer::FILE) {
            client->show_message("Buffer is not a file");
            return;
        }

        directory = buffer->directory.clone(cz::heap_allocator());
        name = buffer->name.clone(cz::heap_allocator());
    }

    run_search(client, editor, directory.buffer, query, query_word, &name);
}

REGISTER_COMMAND(command_search_in_file_prompt);
void command_search_in_file_prompt(Editor* editor, Command_Source source) {
    cz::String selected_region = {};
    CZ_DEFER(selected_region.drop(cz::heap_allocator()));
    get_selected_region(editor, source.client, cz::heap_allocator(), &selected_region);

    Dialog dialog = {};
    dialog.prompt = "Search in current file: ";
    dialog.response_callback = command_search_in_file_callback<false>;
    dialog.mini_buffer_contents = selected_region;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_search_in_file_word_prompt);
void command_search_in_file_word_prompt(Editor* editor, Command_Source source) {
    cz::String selected_region = {};
    CZ_DEFER(selected_region.drop(cz::heap_allocator()));
    get_selected_region(editor, source.client, cz::heap_allocator(), &selected_region);

    Dialog dialog = {};
    dialog.prompt = "Search in current file word: ";
    dialog.response_callback = command_search_in_file_callback<true>;
    dialog.mini_buffer_contents = selected_region;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_search_in_file_token_at_position);
void command_search_in_file_token_at_position(Editor* editor, Command_Source source) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    cz::String name = {};
    CZ_DEFER(name.drop(cz::heap_allocator()));

    cz::String query = {};
    CZ_DEFER(query.drop(cz::heap_allocator()));

    {
        WITH_SELECTED_BUFFER(source.client);
        if (buffer->type != Buffer::FILE) {
            source.client->show_message("Buffer is not a file");
            return;
        }

        directory = buffer->directory.clone(cz::heap_allocator());
        name = buffer->name.clone(cz::heap_allocator());

        Contents_Iterator iterator =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        Token token;
        if (!get_token_at_position(buffer, &iterator, &token)) {
            source.client->show_message("Cursor is not positioned at a token");
            return;
        }

        query.reserve(cz::heap_allocator(), token.end - token.start);
        buffer->contents.slice_into(iterator, token.end, &query);
    }

    ensure_has_empty_file();
    run_search(source.client, editor, directory.buffer, query, true, &name);
}

}
}
