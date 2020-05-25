#pragma once

namespace mag {
struct Editor;
struct Command_Source;

namespace basic {

void command_insert_newline_indent(Editor* editor, Command_Source source);
void command_insert_indent(Editor* editor, Command_Source source);

void command_delete_whitespace(Editor* editor, Command_Source source);

}
}
