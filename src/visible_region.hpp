#pragma once

#include <stddef.h>

namespace mag {
struct Window_Unified;
struct Contents;
struct Contents_Iterator;
struct Mode;

/// Adjust the window such that the iterator is at the center.
void center_in_window(Window_Unified* window, const Mode& mode, Contents_Iterator iterator);

/// Get an iterator to the center of the window.
Contents_Iterator center_of_window(Window_Unified* window, const Contents* contents);

/// Test if the iterator is inside the visible region of the window.
bool is_visible(const Window_Unified* window, const Mode& mode, Contents_Iterator iterator);

/// The scroll outside for a particular window is limited by the number of rows in the window.
size_t get_scroll_outside(size_t rows, size_t scroll_outside);

}
