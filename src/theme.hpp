#pragma once

#include <stdint.h>
#include <cz/vector.hpp>

namespace mag {

struct Color {
    uint8_t r, g, b;
};

struct Face {
    int16_t foreground = -1;
    int16_t background = -1;

    enum Flags {
        BOLD = 1,
        UNDERSCORE = 2,
        REVERSE = 4,
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

    void drop(cz::Allocator allocator) {
        colors.drop(allocator);
        faces.drop(allocator);
    }
};

}
