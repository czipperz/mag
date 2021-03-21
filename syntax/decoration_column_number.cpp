#include "decoration_column_number.hpp"

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

struct Buffer_Data {
    Buffer_Id id;
    size_t change_index;
    uint64_t position;
    uint64_t line_number;
};

static bool decoration_column_number_append(const Buffer* buffer,
                                            Window_Unified* window,
                                            cz::AllocatedString* string,
                                            void* _data) {
    ZoneScoped;

    Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);

    Contents_Iterator line_start = iterator;
    start_of_line(&line_start);

    write(string_writer(string), "C", iterator.position - line_start.position + 1);

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
