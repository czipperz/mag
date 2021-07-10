#include "version_control.hpp"

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
namespace version_control {

bool get_root_directory(Editor* editor,
                        Client* client,
                        const char* dir_cstr,
                        cz::Allocator allocator,
                        cz::String* top_level_path) {
    // Use the current working directory if none is provided.
    if (!dir_cstr) {
        dir_cstr = ".";
    }

    cz::String temp = {};
    CZ_DEFER(temp.drop(cz::heap_allocator()));

    if (cz::path::make_absolute(dir_cstr, cz::heap_allocator(), &temp).is_err()) {
        client->show_message(editor, "Failed to get working directory");
        return false;
    }

    top_level_path->set_len(0);
    top_level_path->reserve(allocator, temp.len() + 1);
    top_level_path->append(temp);
    top_level_path->null_terminate();
    if (cz::find_dir_with_file_up(allocator, top_level_path, ".git")) {
        return true;
    }

    top_level_path->set_len(0);
    top_level_path->append(temp);
    top_level_path->null_terminate();
    if (cz::find_dir_with_file_up(allocator, top_level_path, ".svn")) {
        return true;
    }

    return false;
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
