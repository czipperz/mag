#include "git.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "file.hpp"
#include "message.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace git {

bool get_git_top_level(Client* client,
                       const char* dir_cstr,
                       cz::Allocator allocator,
                       cz::String* top_level_path) {
    cz::Input_File std_out_read;
    CZ_DEFER(std_out_read.close());

    cz::Process process;
    {
        cz::Process_Options options;
        options.working_directory = dir_cstr;

        if (!create_process_output_pipe(&options.std_out, &std_out_read)) {
            client->show_message("Error: I/O operation failed");
            return false;
        }
        options.std_err = options.std_out;
        CZ_DEFER(options.std_out.close());

        cz::Str rev_parse_args[] = {"git", "rev-parse", "--show-toplevel"};
        if (!process.launch_program(cz::slice(rev_parse_args), &options)) {
            client->show_message("No git repository found");
            return false;
        }
    }

    read_to_string(std_out_read, allocator, top_level_path);

    int return_value = process.join();
    if (return_value != 0) {
        client->show_message("No git repository found");
        return false;
    }

    CZ_DEBUG_ASSERT((*top_level_path)[top_level_path->len() - 1] == '\n');
    top_level_path->pop();
    top_level_path->null_terminate();
    return true;
}

static void command_git_grep_callback(Editor* editor, Client* client, cz::Str query, void* data) {
    cz::String top_level_path = {};
    CZ_DEFER(top_level_path.drop(cz::heap_allocator()));
    {
        WITH_BUFFER(*(Buffer_Id*)data);
        if (!get_git_top_level(client, buffer->directory.buffer(), cz::heap_allocator(),
                               &top_level_path)) {
            return;
        }
    }

    cz::Str args[] = {"git", "grep", "-n", "--column", "-e", query, "--", ":/"};

    run_console_command(client, editor, top_level_path.buffer(), cz::slice(args), "git grep",
                        "Git grep error");
}

void command_git_grep(Editor* editor, Command_Source source) {
    Buffer_Id* selected_buffer_id = (Buffer_Id*)malloc(sizeof(Buffer_Id));
    *selected_buffer_id = source.client->selected_window()->id;
    source.client->show_dialog(editor, "git grep: ", no_completion_engine,
                               command_git_grep_callback, selected_buffer_id);

    Transaction transaction;
    {
        Window_Unified* window = source.client->selected_normal_window;
        WITH_WINDOW_BUFFER(window);
        if (!window->show_marks) {
            return;
        }

        uint64_t start = window->cursors[0].start();
        uint64_t end = window->cursors[0].end();
        transaction.init(1, end - start);

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(),
                                            buffer->contents.iterator_at(start), end);
        edit.position = 0;
        edit.flags = Edit::INSERT;
        transaction.push(edit);
    }

    {
        WITH_WINDOW_BUFFER(source.client->mini_buffer_window());
        transaction.commit(buffer);
    }
}

void command_git_grep_token_at_position(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::String top_level_path = {};
    CZ_DEFER(top_level_path.drop(cz::heap_allocator()));
    if (!get_git_top_level(source.client, buffer->directory.buffer(), cz::heap_allocator(),
                           &top_level_path)) {
        source.client->show_message("No git directory");
        return;
    }

    Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);
    Token token;
    if (!get_token_at_position(buffer, &iterator, &token)) {
        source.client->show_message("Cursor is not positioned at a token");
        return;
    }

    cz::String query = {};
    CZ_DEFER(query.drop(cz::heap_allocator()));
    query.reserve(cz::heap_allocator(), token.end - token.start + 4);
    query.push('\\');
    query.push('<');
    buffer->contents.slice_into(iterator, token.end, query.end());
    query.set_len(query.len() + token.end - token.start);
    query.push('\\');
    query.push('>');

    cz::Str args[] = {"git", "grep", "-n", "--column", "-e", query, "--", ":/"};

    run_console_command(source.client, editor, top_level_path.buffer(), cz::slice(args), "git grep",
                        "Git grep error");
}

void command_save_and_quit(Editor* editor, Command_Source source) {
    {
        WITH_SELECTED_BUFFER(source.client);
        if (!save_buffer(buffer)) {
            source.client->show_message("Error saving file");
            return;
        }
    }

    source.client->queue_quit = true;
}

void command_abort_and_quit(Editor* editor, Command_Source source) {
    {
        WITH_SELECTED_BUFFER(source.client);
        clear_buffer(buffer);

        if (!save_buffer(buffer)) {
            source.client->show_message("Error saving file");
            return;
        }
    }

    source.client->queue_quit = true;
}

}
}
