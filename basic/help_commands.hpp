#pragma once

namespace mag {
struct Editor;
struct Command_Source;

namespace basic {

void command_dump_key_map(Editor* editor, Command_Source source);
void command_run_command_by_name(Editor* editor, Command_Source source);

}
}
