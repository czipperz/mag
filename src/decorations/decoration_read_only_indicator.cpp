#include "decoration_read_only_indicator.hpp"

#include <stdlib.h>
#include <cz/format.hpp>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include <tracy/Tracy.hpp>
#include "buffer.hpp"
#include "decoration.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

static bool decoration_read_only_indicator_append(Editor*,
                                                  Client*,
                                                  const Buffer* buffer,
                                                  Window_Unified* window,
                                                  cz::Allocator allocator,
                                                  cz::String* string,
                                                  void* _data) {
    ZoneScoped;

    if (buffer->read_only) {
        cz::append(allocator, string, "Read Only");
        return true;
    }
    return false;
}

static void decoration_read_only_indicator_cleanup(void* _data) {}

Decoration decoration_read_only_indicator() {
    static const Decoration::VTable vtable = {decoration_read_only_indicator_append,
                                              decoration_read_only_indicator_cleanup};
    return {&vtable, nullptr};
}

}
}
