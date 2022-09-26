#include "log.hpp"

#include <cz/format.hpp>
#include "command_macros.hpp"
#include "job.hpp"
#include "movement.hpp"
#include "version_control.hpp"

namespace mag {
namespace version_control {

///////////////////////////////////////////////////////////////////////////////
// command_show_last_commit_to_file
///////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_show_last_commit_to_file);
void command_show_last_commit_to_file(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    cz::String root = {};
    CZ_DEFER(root.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!buffer->get_path(cz::heap_allocator(), &path)) {
            source.client->show_message("Error: file has no path");
            return;
        }

        if (!get_root_directory(buffer->directory.buffer, cz::heap_allocator(), &root)) {
            source.client->show_message("Error: couldn't find vc root");
            return;
        }
    }

    cz::Heap_String buffer_name = cz::format("git last-edit ", path);
    CZ_DEFER(buffer_name.drop());

    cz::Str args[] = {"git", "log", "-1", "-p", "--full-diff", "--", path};
    cz::Arc<Buffer_Handle> handle;
    run_console_command(source.client, editor, root.buffer, args, buffer_name, "Git error",
                        &handle);
}

///////////////////////////////////////////////////////////////////////////////
// command_show_commit
///////////////////////////////////////////////////////////////////////////////

static void command_show_commit_callback(Editor* editor, Client* client, cz::Str commit, void*) {
    cz::String root = {};
    CZ_DEFER(root.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(client);
        if (!get_root_directory(buffer->directory.buffer, cz::heap_allocator(), &root)) {
            client->show_message("Error: couldn't find vc root");
            return;
        }
    }

    cz::Heap_String buffer_name = cz::format("git show ", commit);
    CZ_DEFER(buffer_name.drop());

    cz::Str args[] = {"git", "show", commit};
    cz::Arc<Buffer_Handle> handle;
    run_console_command(client, editor, root.buffer, args, buffer_name, "Git error", &handle);
}

REGISTER_COMMAND(command_show_commit);
void command_show_commit(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Show commit: ";
    dialog.response_callback = command_show_commit_callback;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_show_commit_at_sol);
void command_show_commit_at_sol(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);
    start_of_line(&iterator);

    {
        Contents_Iterator test = iterator;
        for (uint64_t i = 0; i < 8; ++i) {
            if (!cz::is_hex_digit(test.get())) {
                source.client->show_message("No commit on this line");
                return;
            }
            test.advance();
        }
    }

    SSOStr commit = buffer->contents.slice(cz::heap_allocator(), iterator, iterator.position + 8);
    CZ_DEFER(commit.drop(cz::heap_allocator()));
    command_show_commit_callback(editor, source.client, commit.as_str(), nullptr);
}

}
}
