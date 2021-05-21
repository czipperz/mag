#pragma once

#include "command.hpp"

namespace mag {
struct Buffer;
struct Window_Unified;
struct SSOStr;

void insert(Buffer* buffer,
            Window_Unified* window,
            SSOStr value,
            Command_Function committer = nullptr);
void insert_char(Buffer* buffer,
                 Window_Unified* window,
                 char code,
                 Command_Function committer = nullptr);

void delete_regions(Buffer* buffer, Window_Unified* window, Command_Function committer = nullptr);

}
