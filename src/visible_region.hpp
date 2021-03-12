#pragma once

namespace mag {
struct Window;
struct Window_Unified;
struct Contents;
struct Contents_Iterator;

void compute_visible_start(Window* window, Contents_Iterator* line_start_iterator);
void compute_visible_end(Window* window, Contents_Iterator* line_start_iterator);

/// Adjust the window such that the iterator is at the center.
void center_in_window(Window_Unified* window, Contents_Iterator iterator);

/// Get an iterator to the center of the window.
Contents_Iterator center_of_window(Window_Unified* window, const Contents* contents);

/// Test if the iterator is inside the visible region of the window.
bool is_visible(Window_Unified* window, Contents_Iterator iterator);

}
