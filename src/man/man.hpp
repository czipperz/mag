#pragma once

namespace mag {
struct Editor;
struct Command_Source;
struct Completion_Engine_Context;

namespace man {

bool man_completion_engine(Editor*, Completion_Engine_Context* context, bool is_initial_frame);
void command_man(Editor* editor, Command_Source source);
void command_man_at_point(Editor* editor, Command_Source source);

}
}
