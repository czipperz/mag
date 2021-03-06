#include "decoration_read_only_indicator.hpp"

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

static bool decoration_read_only_indicator_append(Buffer* buffer,
                                                  Window_Unified* window,
                                                  cz::AllocatedString* string,
                                                  void* _data) {
    ZoneScoped;

    if (buffer->read_only) {
        write(string_writer(string), "Read Only");
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
