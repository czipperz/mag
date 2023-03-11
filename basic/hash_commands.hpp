#pragma once

namespace mag {
struct Editor;
struct Command_Source;

namespace hash {

void command_insert_divider_60(Editor* editor, Command_Source source);
void command_insert_divider_70(Editor* editor, Command_Source source);
void command_insert_divider_80(Editor* editor, Command_Source source);

void command_insert_header_60(Editor* editor, Command_Source source);
void command_insert_header_70(Editor* editor, Command_Source source);
void command_insert_header_80(Editor* editor, Command_Source source);

}
}
