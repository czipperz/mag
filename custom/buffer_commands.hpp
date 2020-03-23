#pragma once

#include "command.hpp"
#include "client.hpp"
#include "editor.hpp"

namespace mag {
namespace custom {

void command_open_file(Editor* editor, Command_Source source);
void command_save_file(Editor* editor, Command_Source source);

void remove_windows_for_buffer(Client* client, Buffer_Id buffer_id);

void command_switch_buffer(Editor* editor, Command_Source source);
void command_kill_buffer(Editor* editor, Command_Source source);

}
}
