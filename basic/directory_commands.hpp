#pragma once

namespace mag {

struct Editor;
struct Command_Source;

namespace basic {

void command_directory_reload(Editor* editor, Command_Source source);

void command_directory_delete_path(Editor* editor, Command_Source source);
void command_directory_rename_path(Editor* editor, Command_Source source);
void command_directory_open_path(Editor* editor, Command_Source source);

void command_create_directory(Editor* editor, Command_Source source);

}
}
