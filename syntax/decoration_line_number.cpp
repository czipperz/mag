#include "decoration_line_number.hpp"

#include <stdlib.h>
#include <Tracy.hpp>
#include <algorithm>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include <cz/write.hpp>
#include "buffer.hpp"
#include "decoration.hpp"
#include "movement.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

static bool decoration_line_number_append(const Buffer* buffer,
                                          Window_Unified* window,
                                          cz::AllocatedString* string,
                                          void* _data) {
    if (window->show_marks) {
        uint64_t start = buffer->contents.get_line_number(window->cursors[0].start());
        uint64_t end = buffer->contents.get_line_number(window->cursors[0].end());
        write(string_writer(string), 'L', start + 1, '-', end + 1);
    } else {
        uint64_t line = buffer->contents.get_line_number(window->cursors[0].point);
        write(string_writer(string), 'L', line + 1);
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
