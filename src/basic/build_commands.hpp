#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace basic {

void command_build_debug_vc_root(Editor* editor, Command_Source source);

void build_buffer_iterate(Editor* editor, Client* client, bool select_next);
void command_build_open_link_at_point(Editor* editor, Command_Source source);

}
}
