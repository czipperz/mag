#pragma once

#include <cz/slice.hpp>

namespace mag {

struct Client;
struct Editor;
struct Key;

struct Command_Source {
    Client* client;
    cz::Slice<const Key> keys;
};

using Command = void (*)(Editor*, Command_Source);

}
