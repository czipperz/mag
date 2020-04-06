#pragma once

namespace mag {
struct Editor;
struct Command_Source;

namespace man {

extern const char* path_to_autocomplete_man_page;
extern const char* path_to_load_man_page;

void command_man(Editor* editor, Command_Source source);

}
}
