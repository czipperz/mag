#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace cz {
struct Input_File;
}

namespace mag {
namespace basic {

void command_apply_diff(Editor* editor, Command_Source source);

}
}
