#include "git.hpp"

#include <cz/defer.hpp>
#include <cz/file.hpp>
#include <cz/heap.hpp>
#include <cz/path.hpp>
#include <cz/process.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "file.hpp"
#include "message.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace git {

bool get_git_top_level(Editor* editor,
                       Client* client,
                       const char* dir_cstr,
                       cz::Allocator allocator,
                       cz::String* top_level_path) {
    if (cz::path::make_absolute(dir_cstr, allocator, top_level_path).is_err()) {
        client->show_message(editor, "Failed to get working directory");
        return false;
    }

    // Use the null terminator slot to put a trailing `/` if needed.
    if (!top_level_path->ends_with('/')) {
        top_level_path->push('/');
    }

    top_level_path->reserve(allocator, 5);

    while (1) {
        size_t old_len = top_level_path->len();
        top_level_path->append(".git");
        top_level_path->null_terminate();

        if (cz::file::does_file_exist(top_level_path->buffer())) {
            top_level_path->set_len(old_len - 1);
            top_level_path->null_terminate();
            return true;
        }

        top_level_path->set_len(old_len - 1);
        if (!cz::path::pop_name(top_level_path)) {
            return false;
        }
    }
}

void command_save_and_quit(Editor* editor, Command_Source source) {
    {
        WITH_SELECTED_BUFFER(source.client);
        if (!save_buffer(buffer)) {
            source.client->show_message(editor, "Error saving file");
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
            source.client->show_message(editor, "Error saving file");
            return;
        }
    }

    source.client->queue_quit = true;
}

}
}
