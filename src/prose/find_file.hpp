#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace prose {

void command_find_file_in_current_directory(Editor* editor, Command_Source source);
void command_find_file_in_version_control(Editor* editor, Command_Source source);

void command_find_file_diff_master(Editor* editor, Command_Source source);

}
}
