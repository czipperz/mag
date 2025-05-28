#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace basic {

/// From either the search or search result buffer, open the next
/// search result and place the cursor in the result buffer.
void command_iteration_next(Editor* editor, Command_Source source);
void command_iteration_previous(Editor* editor, Command_Source source);

}
}
