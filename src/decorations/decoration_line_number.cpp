#include "decoration_line_number.hpp"

#include <stdlib.h>
#include <algorithm>
#include <cz/format.hpp>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include <tracy/Tracy.hpp>
#include "buffer.hpp"
#include "decoration.hpp"
#include "movement.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

static bool decoration_line_number_append(Editor*,
                                          Client*,
                                          const Buffer* buffer,
                                          Window_Unified* window,
                                          cz::Allocator allocator,
                                          cz::String* string,
                                          void* _data) {
    Cursor cursor = window->cursors[window->selected_cursor];
    if (window->show_marks) {
        uint64_t start = buffer->contents.get_line_number(cursor.start());
        uint64_t end = buffer->contents.get_line_number(cursor.end());
        cz::append(allocator, string, 'L', start + 1, '-', end + 1);
    } else {
        uint64_t line = buffer->contents.get_line_number(cursor.point);
        cz::append(allocator, string, 'L', line + 1);
    }
    return true;
}

static void decoration_line_number_cleanup(void* _data) {}

Decoration decoration_line_number() {
    static const Decoration::VTable vtable = {decoration_line_number_append,
                                              decoration_line_number_cleanup};
    return {&vtable, nullptr};
}

}
}
