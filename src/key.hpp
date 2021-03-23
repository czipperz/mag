#pragma once

#include <limits.h>
#include <stdint.h>

namespace cz {
struct String;
}

namespace mag {

enum Modifiers : uint_fast8_t {
    CONTROL = 1,
    ALT = 2,
    CONTROL_ALT = 3,
    SHIFT = 4,
    CONTROL_SHIFT = 5,
    ALT_SHIFT = 6,
    CONTROL_ALT_SHIFT = 7,
};

namespace Key_Code_ {
/// @AddKeyCode If you add more key codes, update `Key_Map::bind()` and `stringify_key()`.
enum Key_Code : uint16_t {
    BACKSPACE = (uint16_t)UCHAR_MAX + 1,
    INSERT,
    DELETE_,
    HOME,
    END,
    PAGE_UP,
    PAGE_DOWN,

    // Arrow keys
    UP,
    DOWN,
    LEFT,
    RIGHT,

    // Mouse scroll events
    SCROLL_UP,
    SCROLL_DOWN,
    SCROLL_LEFT,
    SCROLL_RIGHT,
    SCROLL_UP_ONE,
    SCROLL_DOWN_ONE,
};
}
using Key_Code_::Key_Code;

struct Key {
    uint_fast8_t modifiers;
    uint16_t code;

    bool operator==(const Key& other) const {
        return modifiers == other.modifiers && code == other.code;
    }

    bool operator<(const Key& other) const {
        if (code != other.code) {
            return code < other.code;
        }
        return modifiers < other.modifiers;
    }
};

constexpr const size_t stringify_key_max_size = 21;

/// Append a key to the string.  Assumes there is enough space
/// (reserve at least `stringify_key_max_size` characters in advance).
void stringify_key(cz::String* prefix, Key key);

}
