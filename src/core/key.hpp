#pragma once

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

namespace cz {
struct String;
struct Str;
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
    GUI = 8,
    GUI_CONTROL = 9,
    GUI_ALT = 10,
    GUI_CONTROL_ALT = 11,
    GUI_SHIFT = 12,
    GUI_CONTROL_SHIFT = 13,
    GUI_ALT_SHIFT = 14,
    GUI_CONTROL_ALT_SHIFT = 15,
};

namespace Key_Code_ {
/// @AddKeyCode If you add more key codes, update `Key::parse()` and `stringify_key()`.
enum Key_Code : uint16_t {
    BACKSPACE = (uint16_t)UCHAR_MAX + 1,
    INSERT,
    DELETE_,
    HOME,
    END,
    PAGE_UP,
    PAGE_DOWN,
    ESCAPE,

    // Arrow keys
    UP,
    DOWN,
    LEFT,
    RIGHT,

    // Function keys
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    // Miscellaneous keys
    MENU,
    SCROLL_LOCK,

    // Mouse events
    MOUSE1,  // left mouse button
    MOUSE2,  // right mouse button
    MOUSE3,  // middle mouse button
    MOUSE4,  // side button usually bound to backwards
    MOUSE5,  // side button usually bound to forwards

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
    uint16_t modifiers;
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

    /// Parse a key description into a `Key`.  If parsing fails, `false` is returned.
    ///
    /// The description describes a single `Key` object.  If the key code is a space write
    /// it as `SPACE` and if it is a non-printable character type it out (ex. `BACKSPACE`).
    /// Prefix `C-`, `A-`, and/or `S-` to add control, alt, and/or shift modifiers.
    ///
    /// Examples:
    /// `c` -- `Key { 0, 'c' }`
    /// `A-b` -- `Key { ALT, 'b' }`
    /// `C-SPACE` -- `Key { CONTROL, ' ' }`
    /// `S-BACKSPACE` -- `Key { SHIFT, BACKSPACE }`
    /// `G-C-A-S-\` -- `Key { GUI | CONTROL | ALT | SHIFT, '\\' }`
    static bool parse(Key* key, cz::Str description);
};

constexpr const size_t stringify_key_max_size = 21;

/// Append a key to the string.  Assumes there is enough space
/// (reserve at least `stringify_key_max_size` characters in advance).
void stringify_key(cz::String* string, Key key);

}
