#pragma once

#include <stdint.h>

namespace mag {

struct Color {
    uint8_t r, g, b;

    constexpr bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b;
    }
    constexpr bool operator!=(const Color& other) const { return !(*this == other); }
};

struct Face_Color {
    bool is_themed;
    union X {
        int16_t theme_index;
        Color color;

        constexpr X(int16_t theme_index) : theme_index(theme_index) {}
        constexpr X(Color color) : color(color) {}
    } x;

    constexpr Face_Color() : Face_Color(-1) {}

    constexpr Face_Color(int16_t theme_index) : is_themed(true), x(theme_index) {}

    constexpr Face_Color(Color color) : is_themed(false), x(color) {}

    constexpr bool operator==(const Face_Color& other) const {
        return is_themed ? (other.is_themed && x.theme_index == other.x.theme_index)
                         : (!other.is_themed && x.color == other.x.color);
    }
    constexpr bool operator!=(const Face_Color& other) const { return !(*this == other); }
};

struct Face {
    Face_Color foreground;
    Face_Color background;

    // @Fragile Note these are linked to `Token_Type_::encode` and `Token_Type_::decode`.  Adding
    // more flags here will break that as it has exactly 5 slots available.  Changing the numbering
    // will also break it but it will be easy to fix.
    enum Flags {
        BOLD = 1,
        UNDERSCORE = 2,
        REVERSE = 4,
        ITALICS = 8,
        INVISIBLE = 16,
    };
    uint32_t flags = 0;

    Face() = default;
    Face(Face_Color foreground, Face_Color background, uint32_t flags)
        : foreground(foreground), background(background), flags(flags) {}
};

}
