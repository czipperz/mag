#include "decoration_pinned_indicator.hpp"

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

static void decoration_pinned_indicator_append(Buffer* buffer,
                                               Window_Unified* window,
                                               cz::AllocatedString* string,
                                               void* _data) {
    ZoneScoped;

    if (window->pinned) {
        write(string_writer(string), "Pinned");
    }
}

static void decoration_pinned_indicator_cleanup(void* _data) {}

Decoration decoration_pinned_indicator() {
    static const Decoration::VTable vtable = {decoration_pinned_indicator_append,
                                              decoration_pinned_indicator_cleanup};
    return {&vtable, nullptr};
}

}
}
