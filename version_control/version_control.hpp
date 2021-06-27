#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace version_control {

bool get_root_directory(Editor* editor,
                        Client* client,
                        const char* dir_cstr,
                        cz::Allocator allocator,
                        cz::String* top_level_path);

void command_save_and_quit(Editor* editor, Command_Source source);
void command_abort_and_quit(Editor* editor, Command_Source source);

void command_find_file(Editor* editor, Command_Source source);
void command_search(Editor* editor, Command_Source source);

}
}
