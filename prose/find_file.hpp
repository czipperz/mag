#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace prose {

void command_find_file_in_current_directory(Editor* editor, Command_Source source);

void command_find_file_in_version_control(Editor* editor, Command_Source source);

}
}
