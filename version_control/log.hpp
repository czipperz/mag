#pragma once

#include "editor.hpp"
#include "command.hpp"

namespace mag {
namespace version_control {

void command_show_last_commit_to_file(Editor* editor, Command_Source source);

}
}