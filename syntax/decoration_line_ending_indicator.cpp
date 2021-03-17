#include "decoration_line_ending_indicator.hpp"

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

static bool decoration_line_ending_indicator_append(const Buffer* buffer,
                                                    Window_Unified* window,
                                                    cz::AllocatedString* string,
                                                    void* _data) {
    ZoneScoped;

    write(string_writer(string), buffer->use_carriage_returns ? "CRLF" : "LF");
    return true;
}

static void decoration_line_ending_indicator_cleanup(void* _data) {}

Decoration decoration_line_ending_indicator() {
    static const Decoration::VTable vtable = {decoration_line_ending_indicator_append,
                                              decoration_line_ending_indicator_cleanup};
    return {&vtable, nullptr};
}

}
}
