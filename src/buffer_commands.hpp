#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {

void command_open_file(Editor* editor, Command_Source source);
void command_save_file(Editor* editor, Command_Source source);

struct Client;
void remove_windows_for_buffer(Client* client, Buffer_Id buffer_id);

void command_kill_buffer(Editor* editor, Command_Source source);

}
