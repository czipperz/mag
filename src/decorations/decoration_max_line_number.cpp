#include "decoration_max_line_number.hpp"

#include <stdlib.h>
#include <algorithm>
#include <cz/format.hpp>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/decoration.hpp"
#include "core/movement.hpp"
#include "core/window.hpp"

namespace mag {
namespace syntax {

static bool decoration_max_line_number_append(Editor*,
                                              Client*,
                                              const Buffer* buffer,
                                              Window_Unified* window,
                                              cz::Allocator allocator,
                                              cz::String* string,
                                              void* _data) {
    uint64_t line = buffer->contents.get_line_number(buffer->contents.len);
    cz::append(allocator, string, '/', line);
    return true;
}

static void decoration_max_line_number_cleanup(void* _data) {}

Decoration decoration_max_line_number() {
    static const Decoration::VTable vtable = {decoration_max_line_number_append,
                                              decoration_max_line_number_cleanup};
    return {&vtable, nullptr};
}

}
}
