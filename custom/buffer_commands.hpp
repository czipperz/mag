#pragma once

#include "command.hpp"
#include "client.hpp"
#include "editor.hpp"

namespace custom {

void command_open_file(mag::Editor* editor, mag::Command_Source source);
void command_save_file(mag::Editor* editor, mag::Command_Source source);

void remove_windows_for_buffer(mag::Client* client, mag::Buffer_Id buffer_id);

void command_switch_buffer(mag::Editor* editor, mag::Command_Source source);
void command_kill_buffer(mag::Editor* editor, mag::Command_Source source);

}
