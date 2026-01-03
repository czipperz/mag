#pragma once

#include "core/client.hpp"
#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace basic {

void command_realign_table(Editor* editor, Command_Source source);

void command_csv_to_table(Editor* editor, Command_Source source);

}
}
