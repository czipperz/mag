#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace cz {
struct Input_File;
}

namespace mag {
namespace basic {

void command_apply_diff(Editor* editor, Command_Source source);

}
}
