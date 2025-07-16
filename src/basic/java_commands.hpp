#pragma once

namespace mag {
struct Editor;
struct Command_Source;

namespace basic {

/// Open token at point as a path.
void command_java_open_token_at_point(Editor* editor, Command_Source source);

}
}
