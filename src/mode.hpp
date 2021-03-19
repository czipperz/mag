#pragma once

#include <stdint.h>
#include <cz/slice.hpp>

namespace mag {

struct Contents_Iterator;
struct Token;
struct Key_Map;
struct Decoration;
struct Overlay;

struct Mode {
    Key_Map* key_map;

    bool (*next_token)(Contents_Iterator* iterator /* in/out */,
                       Token* token /* out */,
                       uint64_t* state /* in/out */);

    cz::Slice<Decoration> decorations;
    cz::Slice<Overlay> overlays;

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

    /// Increase the level of indentation on the line after an open pair automatically.
    bool indent_after_open_pair = false;
};

inline bool default_next_token(Contents_Iterator*, Token*, uint64_t*) {
    return false;
}

}
