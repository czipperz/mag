#pragma once

namespace mag {
struct Editor;
struct Command_Source;

namespace basic {

void command_insert_open_pair(Editor* editor, Command_Source source);
void command_insert_close_pair(Editor* editor, Command_Source source);

}
}
