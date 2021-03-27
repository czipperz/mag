#pragma once

#include <stdint.h>
#include <cz/vector.hpp>
#include "completion.hpp"
#include "face.hpp"

namespace mag {
struct Decoration;
struct Overlay;

struct Theme {
    const char* font_file;
    uint32_t font_size;

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
    ///
    /// `colors` should be `256` elements long.
    uint32_t* colors;

    /// The way different token types should be displayed.  These should
    /// directly correspond to the token types.
    cz::Vector<Face> faces;

    cz::Slice<Decoration> decorations;
    cz::Slice<Overlay> overlays;

    size_t max_completion_results;

    Completion_Filter mini_buffer_completion_filter;
    Completion_Filter window_completion_filter;

    /// The number of rows a mouse scroll event should move.
    /// Used in `command_scroll_down` and `command_scroll_up`.
    uint32_t mouse_scroll_rows = 4;

    void drop(cz::Allocator allocator) {
        faces.drop(allocator);
    }
};

}
