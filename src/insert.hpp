#pragma once

#include "command.hpp"

namespace mag {
struct Buffer;
struct Window_Unified;
struct SSOStr;

void insert(Client* client,
            Buffer* buffer,
            Window_Unified* window,
            SSOStr value,
            Command_Function committer = nullptr);
void insert_char(Client* client,
                 Buffer* buffer,
                 Window_Unified* window,
                 char code,
                 Command_Function committer = nullptr);

void delete_regions(Client* client,
                    Buffer* buffer,
                    Window_Unified* window,
                    Command_Function committer = nullptr);

void command_insert_char(Editor* editor, Command_Source source);
void do_command_insert_char(Client* client,
                            Buffer* buffer,
                            Window_Unified* window,
                            Command_Source source);

}
