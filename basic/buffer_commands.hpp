#pragma once

#include "client.hpp"
#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_open_file(Editor* editor, Command_Source source);
void command_save_file(Editor* editor, Command_Source source);

void remove_windows_for_buffer(Client* client, Buffer_Id buffer_id);

void command_switch_buffer(Editor* editor, Command_Source source);
void command_kill_buffer(Editor* editor, Command_Source source);

void command_rename_buffer(Editor* editor, Command_Source source);
void command_save_buffer_to(Editor* editor, Command_Source source);

}
}
