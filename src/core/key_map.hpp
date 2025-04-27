#pragma once

#include <cz/str.hpp>
#include <cz/vector.hpp>
#include "core/command.hpp"
#include "core/key.hpp"

namespace mag {

struct Key_Map;

struct Key_Bind {
    Key key;
    bool is_command;
    union {
        Command command;
        Key_Map* map;
    } v;

    bool operator<(const Key_Bind& other) const { return key < other.key; }
};

struct Key_Map {
    cz::Vector<Key_Bind> bindings;

    /// Bind a key description to a command.  See `Key::parse` for more details.
    void bind(cz::Str description, Command command);

    /// Lookup the Key_Bind for a specific key.  This does a binary search of the
    /// bindings.
    const Key_Bind* lookup(Key key) const;

    void drop();
};

}
