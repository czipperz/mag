#include "decoration_line_number.hpp"

#include <stdlib.h>
#include <Tracy.hpp>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include <cz/write.hpp>
#include "buffer.hpp"
#include "decoration.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

static void decoration_line_number_append(Buffer* buffer,
                                          Window_Unified* window,
                                          cz::AllocatedString* string,
                                          void* _data) {
    ZoneScoped;

    uint64_t line_number = 0;
    uint64_t line_start = 0;
    Contents_Iterator iterator = buffer->contents.start();
    while (iterator.position < window->cursors[0].point) {
        if (iterator.get() == '\n') {
            ++line_number;
            line_start = iterator.position + 1;
        }
        iterator.advance();
    }

    write(string_writer(string), 'L', line_number + 1, " C", iterator.position - line_start + 1);
}

static void decoration_line_number_cleanup(void* _data) {}

Decoration decoration_line_number() {
    static const Decoration::VTable vtable = {decoration_line_number_append,
                                              decoration_line_number_cleanup};
    return {&vtable, nullptr};
}

}
}
