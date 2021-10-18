#pragma once

namespace mag {
struct Editor;
struct Command_Source;

namespace basic {

void command_complete_at_point_prompt_identifiers_or_cmake_keywords(Editor* editor,
                                                                    Command_Source source);

}
}
