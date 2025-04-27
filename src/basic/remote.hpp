#pragma once

#include "editor.hpp"

namespace mag {
namespace basic {

int start_server(Editor* editor);
void kill_server();

int client_connect_and_open(cz::Str file);

void command_start_server(Editor* editor, Command_Source source);
void command_kill_server(Editor* editor, Command_Source source);

}
}
