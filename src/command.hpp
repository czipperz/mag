#pragma once

#include <cz/slice.hpp>

namespace mag {

struct Client;
struct Editor;
struct Key;

struct Command_Source;

using Command = void (*)(Editor*, Command_Source);

struct Command_Source {
    Client* client;
    cz::Slice<const Key> keys;
    Command previous_command;
};

}
