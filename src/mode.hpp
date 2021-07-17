#pragma once

#include <stdint.h>
#include <cz/heap_vector.hpp>
#include <cz/slice.hpp>
#include "case.hpp"
#include "key_map.hpp"
#include "token.hpp"

namespace mag {

struct Contents_Iterator;
struct Decoration;
struct Overlay;

struct Mode {
    Key_Map key_map;
    Key_Map completion_key_map;

    Tokenizer next_token;

    cz::Heap_Vector<Decoration> decorations;
    cz::Heap_Vector<Overlay> overlays;

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
    /// tab_width = 4; // still set tab_width just in case tabs are used
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

    /// Stores the preferred max line length.
    uint64_t preferred_column = 100;

    /// If `true`, then long lines will be displayed as multiple visual lines.
    bool wrap_long_lines = true;

    /// Increase the level of indentation on the line after an open pair automatically.
    bool indent_after_open_pair = false;

    /// Controls whether searching in this buffer will handle case sensitivity.
    /// `search_prompt_case_handling` is used when the search has a prompt.
    /// `search_continue_case_handling` is used when the search is based off a selected region.
    Case_Handling search_prompt_case_handling = Case_Handling::UPPERCASE_STICKY;
    Case_Handling search_continue_case_handling = Case_Handling::CASE_SENSITIVE;

    /// If enabled then the various comment commands will break tabs
    /// into spaces at and after the point the comments are inserted.
    ///
    /// This causes the lines that are indented more in the comment
    /// section to look as if they are still indented more than the least
    /// indented lines instead of the tab absorbing the comment start.
    ///
    /// If `tab_width != indent_width` and this option is disabled then the comments
    /// will be put after the last tab making the resulting indentation look weird.
    ///
    /// Uncommenting will coalesce spaces into tabs regardless of whether this is set.
    bool comment_break_tabs = true;

    /// Used for debugging.
    bool render_bucket_boundaries = false;

    void drop();
};

inline bool default_next_token(Contents_Iterator*, Token*, uint64_t*) {
    return false;
}

}
