#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace git {

bool get_git_top_level(Client* client,
                       const char* dir_cstr,
                       cz::Allocator allocator,
                       cz::String* top_level_path);

void command_git_grep(Editor* editor, Command_Source source);

void command_save_and_quit(Editor* editor, Command_Source source);
void command_abort_and_quit(Editor* editor, Command_Source source);

}
}
