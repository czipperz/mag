#pragma once

namespace mag {
struct Editor;
struct Command_Source;
}

namespace clang_format {

void command_clang_format_buffer(mag::Editor* editor, mag::Command_Source source);

}
