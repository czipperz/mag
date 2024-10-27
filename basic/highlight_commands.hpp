#pragma once

namespace mag {
struct Editor;
struct Command_Source;

namespace basic {

void command_add_highlight_to_buffer(Editor* editor, Command_Source source);
void command_remove_highlight_from_buffer(Editor* editor, Command_Source source);

}
}
