#include "version_control.hpp"

#include <cz/defer.hpp>
#include <cz/find_file.hpp>
#include <cz/heap.hpp>
#include <cz/path.hpp>
#include "core/client.hpp"
#include "core/command_macros.hpp"
#include "core/file.hpp"
#include "core/message.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"

namespace mag {
namespace version_control {

bool get_root_directory(cz::Str directory, cz::Allocator allocator, cz::String* top_level_path) {
    cz::String temp = {};
    CZ_DEFER(temp.drop(cz::heap_allocator()));

    if (!cz::path::make_absolute(directory, cz::heap_allocator(), &temp)) {
        return false;
    }

    top_level_path->len = 0;
    top_level_path->reserve(allocator, temp.len + 1);
    top_level_path->append(temp);
    top_level_path->null_terminate();
    if (cz::find_dir_with_file_up(allocator, top_level_path, ".git")) {
        return true;
    }

    top_level_path->len = 0;
    top_level_path->append(temp);
    top_level_path->null_terminate();
    if (cz::find_dir_with_file_up(allocator, top_level_path, ".svn")) {
        return true;
    }

    return false;
}

REGISTER_COMMAND(command_save_and_quit);
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

REGISTER_COMMAND(command_abort_and_quit);
void command_abort_and_quit(Editor* editor, Command_Source source) {
    {
        WITH_SELECTED_BUFFER(source.client);
        clear_buffer(source.client, buffer);

        if (!save_buffer(buffer)) {
            source.client->show_message("Error saving file");
            return;
        }
    }

    source.client->queue_quit = true;
}

}
}
