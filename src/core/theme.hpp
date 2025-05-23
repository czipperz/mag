#pragma once

#include <stdint.h>
#include <cz/heap_vector.hpp>
#include "core/completion.hpp"
#include "core/face.hpp"
#include "core/token.hpp"

namespace mag {
struct Decoration;
struct Overlay;

namespace Face_Types_ {
enum Face_Type : uint64_t {
    DEFAULT_MODE_LINE,
    UNSAVED_MODE_LINE,
    SELECTED_MODE_LINE,

    SELECTED_CURSOR,
    SELECTED_REGION,
    OTHER_CURSOR,
    OTHER_REGION,

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

    cz::Heap_Vector<Decoration> decorations;
    cz::Heap_Vector<Overlay> overlays;

    size_t max_completion_results = 5;
    size_t mini_buffer_max_height = 1;

    Completion_Filter mini_buffer_completion_filter;
    Completion_Filter window_completion_filter;

    /// The number of rows a mouse scroll event should move.
    /// Used in `command_scroll_down` and `command_scroll_up`.
    uint32_t mouse_scroll_rows = 4;

    /// The number of columns a mouse scroll event should move.
    /// Used in `command_scroll_left` and `command_scroll_right`.
    uint32_t mouse_scroll_cols = 4;

    /// If true then draw line numbers.
    bool draw_line_numbers = false;

    /// If true then scrolling within the same file will be animated.
    bool allow_animated_scrolling = false;

    /// If the point is within this many rows of the top or
    /// bottom of the `Window` then the `Window` scrolls.
    size_t scroll_outside_visual_rows = 0;

    /// If true then scrolls at least half a page when the cursor exits the visible region.
    bool scroll_jump_half_page_when_outside_visible_region = false;

    /// If `wrap_long_lines` is enabled and the point is within this many columns of
    /// the left or right side of the `Window` then the `Window` scrolls horizontally.
    size_t scroll_outside_visual_columns = 0;

    /// When inserting text, replace existing text.
    bool insert_replace = false;

    void drop();
};

}
