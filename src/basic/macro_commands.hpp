#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace basic {

void command_start_recording_macro(Editor* editor, Command_Source source);
void command_stop_recording_macro(Editor* editor, Command_Source source);
void command_run_macro(Editor* editor, Command_Source source);
void command_run_macro_forall_lines_in_search(Editor* editor, Command_Source source);
void command_print_macro(Editor* editor, Command_Source source);

}
}
