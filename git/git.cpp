#include "git.hpp"

#include <cz/defer.hpp>
#include <cz/file.hpp>
#include <cz/find_file.hpp>
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
    // Use the current working directory if none is provided.
    if (!dir_cstr) {
        dir_cstr = ".";
    }

    if (cz::path::make_absolute(dir_cstr, allocator, top_level_path).is_err()) {
        client->show_message(editor, "Failed to get working directory");
        return false;
    }

    return cz::find_dir_with_file_up(allocator, top_level_path, ".git");
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
