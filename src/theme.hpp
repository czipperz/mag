#pragma once

#include <stdint.h>
#include <cz/vector.hpp>
#include "completion.hpp"
#include "face.hpp"
#include "token.hpp"

namespace mag {
struct Decoration;
struct Overlay;

namespace Face_Types_ {
enum Face_Type : uint64_t {
    DEFAULT_MODE_LINE,
    UNSAVED_MODE_LINE,
    SELECTED_MODE_LINE,

    CURSOR,
    MARKED_REGION,

    MINI_BUFFER_PROMPT,
    MINI_BUFFER_COMPLETION_SELECTED,

    WINDOW_COMPLETION_NORMAL,
    WINDOW_COMPLETION_SELECTED,

    LINE_NUMBER,
    LINE_NUMBER_RIGHT_PADDING,
    LINE_NUMBER_LEFT_PADDING,

    SEARCH_MODE_RESULT_HIGHLIGHT,

    // Special value representing the number of values in the enum.
    length,
};
}
using Face_Types_::Face_Type;

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

    /// The way different things are rendered.
    Face special_faces[Face_Type::length];

    /// The way different token types should be displayed.
    /// These should directly correspond to the token types.
    Face token_faces[Token_Type::length];

    cz::Vector<Decoration> decorations;
    cz::Vector<Overlay> overlays;

    size_t max_completion_results;

    Completion_Filter mini_buffer_completion_filter;
    Completion_Filter window_completion_filter;

    /// The number of rows a mouse scroll event should move.
    /// Used in `command_scroll_down` and `command_scroll_up`.
    uint32_t mouse_scroll_rows = 4;

    /// If true then draw line numbers.
    bool draw_line_numbers = false;

    void drop(cz::Allocator allocator);
};

}
