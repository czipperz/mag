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

static void command_git_grep_callback(Editor* editor, Client* client, cz::Str query, void* data) {
    size_t backslashes = 0;
    for (size_t i = 0; i < query.len; ++i) {
        if (add_backslash(query[i])) {
            ++backslashes;
        }
    }

    cz::String script = {};
    CZ_DEFER(script.drop(cz::heap_allocator()));
    cz::Str prefix = "git grep \"";
    script.reserve(cz::heap_allocator(), prefix.len + query.len + backslashes + 2);
    script.append(prefix);
    for (size_t i = 0; i < query.len; ++i) {
        if (add_backslash(query[i])) {
            script.push('\\');
        }
        script.push(query[i]);
    }
    script.push('"');
    script.null_terminate();

    cz::String results = {};
    CZ_DEFER(results.drop(cz::heap_allocator()));
    int return_value = 0;
    if (!run_script_synchronously(script.buffer(), nullptr, cz::heap_allocator(), &results,
                                  &return_value) ||
        return_value != 0) {
        Message message = {};
        message.tag = Message::SHOW;
        message.text = "Git grep error";
        client->show_message(message);
        return;
    }

    Buffer_Id buffer_id = editor->create_temp_buffer("git grep");
    WITH_BUFFER(buffer_id, { buffer->contents.insert(0, results); });

    client->set_selected_buffer(buffer_id);
}

void command_git_grep(Editor* editor, Command_Source source) {
    Message message = {};
    message.tag = Message::RESPOND_TEXT;
    message.text = "git grep: ";
    message.response_callback = command_git_grep_callback;
    source.client->show_message(message);
}

}
}
