#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace version_control {

/// Get the root directory for a version control repo that contains `file`.
/// Returns `false` if no repo is found.
bool get_root_directory(cz::Str file, cz::Allocator allocator, cz::String* top_level_path);

void command_save_and_quit(Editor* editor, Command_Source source);
void command_abort_and_quit(Editor* editor, Command_Source source);

void command_find_file(Editor* editor, Command_Source source);
void command_search(Editor* editor, Command_Source source);

}
}
