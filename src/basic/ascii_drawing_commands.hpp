#pragma once

namespace mag {
struct Command_Source;
struct Editor;

namespace ascii_drawing {

void command_draw_box(Editor* editor, Command_Source source);
void command_insert_indent_width_as_spaces(Editor* editor, Command_Source source);
void command_delete_backwards_indent_width_as_spaces(Editor* editor, Command_Source source);

}
}
