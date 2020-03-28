#pragma once

namespace mag {
struct Window;
struct Contents_Iterator;

void compute_visible_start(Window* window, Contents_Iterator* line_start_iterator);
void compute_visible_end(Window* window, Contents_Iterator* line_start_iterator);

}
