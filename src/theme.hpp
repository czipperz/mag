#pragma once

#include <stdint.h>
#include <cz/vector.hpp>
#include "completion.hpp"
#include "face.hpp"

namespace mag {
struct Decoration;
struct Overlay;

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

    /// The number of columns in a level of indentation.
    ///
    /// To indent 2 spaces but then 8 spaces are converted to tabs use:
    /// ```
    /// indent_width = 2;
    /// tab_width = 8;
    /// use_tabs = true;
    /// ```
    ///
    /// To indent 4 spaces and never use tabs use:
    /// ```
    /// indent_width = 4;
    /// // tab_column_width is arbitrary
    /// use_tabs = false;
    /// ```
    ///
    /// To indent using one tab and display tabs as 4 wide use:
    /// ```
    /// indent_width = tab_width = 4;
    /// use_tabs = true;
    /// ```
    uint32_t indent_width = 4;

    /// The number of columns a tab takes up.
    uint32_t tab_width = 4;

    /// If `true`, prefers tabs over spaces as long as they fit.
    bool use_tabs = false;

    void drop(cz::Allocator allocator) {
        faces.drop(allocator);
    }
};

}
