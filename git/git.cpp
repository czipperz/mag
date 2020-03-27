#include "git.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "message.hpp"
#include "process.hpp"

namespace mag {
namespace git {

static bool run_console_command(Client* client,
                                Editor* editor,
                                const char* working_directory,
                                const char* script,
                                cz::Str buffer_name,
                                cz::Str error) {
    cz::String results = {};
    CZ_DEFER(results.drop(cz::heap_allocator()));

    // Todo: Make this a job
    Process process;
    if (!process.launch_script(script, working_directory)) {
        client->show_message(error);
        return false;
    }
    process.read_to_string(cz::heap_allocator(), &results);
    int return_value = process.join();
    process.destroy();
    if (return_value != 0) {
        client->show_message(error);
        return false;
    }

    Buffer_Id buffer_id = editor->create_temp_buffer(buffer_name);
    {
        WITH_BUFFER(buffer_id);
        buffer->contents.insert(0, working_directory);
        buffer->contents.insert(buffer->contents.len, ": ");
        buffer->contents.insert(buffer->contents.len, script);
        buffer->contents.insert(buffer->contents.len, "\n");
        buffer->contents.insert(buffer->contents.len, results);
    }

    client->set_selected_buffer(buffer_id);
    return true;
}

static bool get_git_top_level(Client* client,
                              cz::Str buffer_path,
                              cz::Allocator allocator,
                              cz::String* top_level_path) {
    const char* buffer_path_end = buffer_path.rfind('/');

    cz::String dir = {};
    CZ_DEFER(dir.drop(cz::heap_allocator()));

    const char* dir_cstr;
    if (buffer_path_end) {
        ++buffer_path_end;

        size_t dir_len = buffer_path_end - buffer_path.buffer;
        if (dir_len == buffer_path.len) {
            dir_cstr = buffer_path.buffer;
        } else {
            dir.reserve(cz::heap_allocator(), dir_len);
            dir.append({buffer_path.buffer, dir_len});
            dir_cstr = dir.buffer();
        }
    } else {
        dir_cstr = nullptr;
    }

    Process process;
    if (!process.launch_script("git rev-parse --show-toplevel", dir_cstr)) {
        client->show_message("No git repository found");
        return false;
    }
    process.read_to_string(allocator, top_level_path);
    int return_value = process.join();
    process.destroy();
    if (return_value != 0) {
        client->show_message("No git repository found");
        return false;
    }

    CZ_DEBUG_ASSERT((*top_level_path)[top_level_path->len() - 1] == '\n');
    top_level_path->pop();
    top_level_path->null_terminate();
    return true;
}

static bool add_backslash(char c) {
    switch (c) {
    case '!':
    case '"':
    case '$':
    case '\\':
    case '`':
        return true;

    default:
        return false;
    }
}

static void command_git_grep_callback(Editor* editor, Client* client, cz::Str query, void* data) {
    cz::String top_level_path = {};
    CZ_DEFER(top_level_path.drop(cz::heap_allocator()));
    {
        WITH_BUFFER(*(Buffer_Id*)data);
        get_git_top_level(client, buffer->path, cz::heap_allocator(), &top_level_path);
    }

    size_t backslashes = 0;
    for (size_t i = 0; i < query.len; ++i) {
        if (add_backslash(query[i])) {
            ++backslashes;
        }
    }

    cz::String script = {};
    CZ_DEFER(script.drop(cz::heap_allocator()));
    cz::Str prefix = "git grep -n --column -e \"";
    cz::Str postfix = "\" -- :/";
    script.reserve(cz::heap_allocator(), prefix.len + query.len + backslashes + postfix.len + 1);
    script.append(prefix);
    for (size_t i = 0; i < query.len; ++i) {
        if (add_backslash(query[i])) {
            script.push('\\');
        }
        script.push(query[i]);
    }
    script.append(postfix);
    script.null_terminate();

    run_console_command(client, editor, top_level_path.buffer(), script.buffer(), "git grep",
                        "Git grep error");
}

void command_git_grep(Editor* editor, Command_Source source) {
    Buffer_Id* selected_buffer_id = (Buffer_Id*)malloc(sizeof(Buffer_Id));
    *selected_buffer_id = source.client->selected_window()->id;
    source.client->show_dialog(editor, "git grep: ", no_completion_engine,
                               command_git_grep_callback, selected_buffer_id);
}

}
}
