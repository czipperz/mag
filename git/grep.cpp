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

    Transaction transaction = {};
    CZ_DEFER(transaction.drop());
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
    buffer->contents.slice_into(iterator, token.end, &query);
    query.push('\\');
    query.push('>');

    cz::Str args[] = {"git", "grep", "-n", "--column", "-e", query, "--", ":/"};

    run_console_command(source.client, editor, top_level_path.buffer(), cz::slice(args), "git grep",
                        "Git grep error");
}

}
}
