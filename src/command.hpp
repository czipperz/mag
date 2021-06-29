#pragma once

#include <cz/slice.hpp>

namespace mag {

struct Client;
struct Editor;
struct Key;

struct Command_Source;

using Command_Function = void (*)(Editor*, Command_Source);

struct Command {
    Command_Function function;
    const char* string;
};

struct Command_Source {
    Client* client;
    cz::Slice<const Key> keys;
    Command previous_command;
};

}
