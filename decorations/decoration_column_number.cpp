#include "decoration_column_number.hpp"

#include <stdlib.h>
#include <Tracy.hpp>
#include <algorithm>
#include <cz/format.hpp>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include "buffer.hpp"
#include "decoration.hpp"
#include "movement.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

static bool decoration_column_number_append(Editor*,
                                            Client*,
                                            const Buffer* buffer,
                                            Window_Unified* window,
                                            cz::Allocator allocator,
                                            cz::String* string,
                                            void* _data) {
    ZoneScoped;

    Contents_Iterator iterator =
        buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);

    Contents_Iterator line_start = iterator;
    start_of_line(&line_start);

    cz::append(allocator, string, "C", iterator.position - line_start.position + 1);

    return true;
}

static void decoration_column_number_cleanup(void* _data) {}

Decoration decoration_column_number() {
    static const Decoration::VTable vtable = {decoration_column_number_append,
                                              decoration_column_number_cleanup};
    return {&vtable, nullptr};
}

}
}
