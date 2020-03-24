#include "git.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "message.hpp"
#include "process.hpp"

namespace mag {
namespace git {

static bool add_backslash(char c) {
    switch (c) {
    case ' ':
    case '!':
    case '"':
    case '#':
    case '$':
    case '&':
    case '\'':
    case '(':
    case ')':
    case '*':
    case ',':
    case ';':
    case '<':
    case '>':
    case '?':
    case '[':
    case '\\':
    case ']':
    case '^':
    case '`':
    case '{':
    case '|':
    case '}':
        return true;

    default:
        return false;
    }
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

    int return_value = 0;
    if (!run_script_synchronously("git rev-parse --show-toplevel", dir_cstr, allocator,
                                  top_level_path, &return_value) ||
        return_value != 0) {
        Message message = {};
        message.tag = Message::SHOW;
        message.text = "No git repository found";
        client->show_message(message);
        return false;
    }

    CZ_DEBUG_ASSERT((*top_level_path)[top_level_path->len() - 1] == '\n');
    top_level_path->pop();
    top_level_path->null_terminate();
    return true;
}

static void command_git_grep_callback(Editor* editor, Client* client, cz::Str query, void* data) {
    size_t backslashes = 0;
    for (size_t i = 0; i < query.len; ++i) {
        if (add_backslash(query[i])) {
            ++backslashes;
        }
    }

    cz::String top_level_path = {};
    CZ_DEFER(top_level_path.drop(cz::heap_allocator()));
    WITH_BUFFER(*(Buffer_Id*)data, {
        get_git_top_level(client, buffer->path, cz::heap_allocator(), &top_level_path);
    });

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

    cz::String results = {};
    CZ_DEFER(results.drop(cz::heap_allocator()));
    int return_value = 0;
    if (!run_script_synchronously(script.buffer(), top_level_path.buffer(), cz::heap_allocator(),
                                  &results, &return_value) ||
        return_value != 0) {
        Message message = {};
        message.tag = Message::SHOW;
        message.text = "Git grep error";
        client->show_message(message);
        return;
    }

    Buffer_Id buffer_id = editor->create_temp_buffer("git grep");
    WITH_BUFFER(buffer_id, {
        buffer->contents.insert(0, top_level_path);
        buffer->contents.insert(buffer->contents.len, ": ");
        buffer->contents.insert(buffer->contents.len, script);
        buffer->contents.insert(buffer->contents.len, "\n");
        buffer->contents.insert(buffer->contents.len, results);
    });

    client->set_selected_buffer(buffer_id);
}

void command_git_grep(Editor* editor, Command_Source source) {
    Message message = {};
    message.tag = Message::RESPOND_TEXT;
    message.text = "git grep: ";
    message.response_callback = command_git_grep_callback;

    Buffer_Id* selected_buffer_id = (Buffer_Id*)malloc(sizeof(Buffer_Id));
    *selected_buffer_id = source.client->selected_window()->id;
    message.response_callback_data = selected_buffer_id;

    source.client->show_message(message);
}

}
}
