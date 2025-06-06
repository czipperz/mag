#include "decoration_pinned_indicator.hpp"

#include <stdlib.h>
#include <cz/format.hpp>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/decoration.hpp"
#include "core/window.hpp"

namespace mag {
namespace syntax {

static bool decoration_pinned_indicator_append(Editor*,
                                               Client*,
                                               const Buffer* buffer,
                                               Window_Unified* window,
                                               cz::Allocator allocator,
                                               cz::String* string,
                                               void* _data) {
    ZoneScoped;

    if (window->pinned) {
        cz::append(allocator, string, "Pinned");
        return true;
    }
    return false;
}

static void decoration_pinned_indicator_cleanup(void* _data) {}

Decoration decoration_pinned_indicator() {
    static const Decoration::VTable vtable = {decoration_pinned_indicator_append,
                                              decoration_pinned_indicator_cleanup};
    return {&vtable, nullptr};
}

}
}
