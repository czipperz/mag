#pragma once

#include <stdint.h>

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
enum Key_Code : uint16_t {
    BACKSPACE,
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

}
