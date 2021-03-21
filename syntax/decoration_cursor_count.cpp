#include "decoration_cursor_count.hpp"

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

static bool decoration_cursor_count_append(const Buffer* buffer,
                                           Window_Unified* window,
                                           cz::AllocatedString* string,
                                           void* _data) {
    ZoneScoped;

    if (window->cursors.len() <= 1) {
        return false;
    }

    write(string_writer(string), "(", window->cursors.len(), ")");
    return true;
}

static void decoration_cursor_count_cleanup(void* _data) {}

Decoration decoration_cursor_count() {
    static const Decoration::VTable vtable = {decoration_cursor_count_append,
                                              decoration_cursor_count_cleanup};
    return {&vtable, nullptr};
}

}
}
