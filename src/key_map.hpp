#pragma once

#include <cz/vector.hpp>
#include "command.hpp"
#include "key.hpp"

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

    /// Bind a key description to a command.
    ///
    /// The description describes a chain of Key objects separated by spaces.  If
    /// the key code is a space or a backslash escape it with another backslash.
    /// Prefix C- and/or A- to add control and alt modifiers.
    ///
    /// Examples:
    /// "C-\\ " -- [Key { CONTROL, ' ' }]
    /// "A-b c" -- [Key { ALT, 'b' }, Key { 0, 'c' }]
    void bind(cz::Str description, Command command);

    /// Lookup the Key_Bind for a specific key.  This does a binary search of the
    /// bindings.
    Key_Bind* lookup(Key key);

    void drop();
};

}
