#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_configure(Editor* editor, Command_Source source);
void command_toggle_read_only(Editor* editor, Command_Source source);
void command_toggle_pinned(Editor* editor, Command_Source source);
void command_toggle_draw_line_numbers(Editor* editor, Command_Source source);
void command_toggle_line_feed(Editor* editor, Command_Source source);
void command_toggle_render_bucket_boundaries(Editor* editor, Command_Source source);
void command_toggle_use_tabs(Editor* editor, Command_Source source);
void command_toggle_animated_scrolling(Editor* editor, Command_Source source);
void command_toggle_wrap_long_lines(Editor* editor, Command_Source source);
void command_toggle_insert_replace(Editor* editor, Command_Source source);

}
}
