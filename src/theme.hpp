#pragma once

#include <stdint.h>
#include <cz/vector.hpp>
#include "completion.hpp"

namespace mag {
struct Decoration;
struct Overlay;

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
        if (is_themed) {
            return other.is_themed && x.theme_index == other.x.theme_index;
        } else {
            return !other.is_themed && x.color == other.x.color;
        }
    }
    constexpr bool operator!=(const Face_Color& other) const { return !(*this == other); }
};

struct Face {
    Face_Color foreground;
    Face_Color background;

    enum Flags {
        BOLD = 1,
        UNDERSCORE = 2,
        REVERSE = 4,
        ITALICS = 8,
        INVISIBLE = 16,
    };
    uint32_t flags = 0;
};

struct Theme {
    /// The colors to be defined.  Note that they are not always available.
    ///
    /// Color 0 should be the default background color.  Color 7 should be the
    /// default foreground color.
    ///
    /// In ncurses, the following colors are defined by default.  Please be
    /// cognizant of them when designing a theme.
    ///
    /// BLACK   0
    /// RED     1
    /// GREEN   2
    /// YELLOW  3
    /// BLUE    4
    /// MAGENTA 5
    /// CYAN    6
    /// WHITE   7
    cz::Vector<Color> colors;

    /// The way different token types should be displayed.  These should
    /// directly correspond to the token types.
    cz::Vector<Face> faces;

    cz::Slice<Decoration> decorations;
    cz::Slice<Overlay> overlays;

    size_t max_completion_results;

    Completion_Filter completion_filter;

    void drop(cz::Allocator allocator) {
        colors.drop(allocator);
        faces.drop(allocator);
    }
};

}
