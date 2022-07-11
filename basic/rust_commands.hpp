#pragma once

namespace mag {
struct Editor;
struct Command_Source;

namespace rust {

void command_extract_variable(Editor* editor, Command_Source source);

}
}
