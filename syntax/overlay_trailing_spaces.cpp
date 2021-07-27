#include "overlay_trailing_spaces.hpp"

#include <cz/char_type.hpp>
#include <cz/heap.hpp>
#include "overlay.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

namespace overlay_trailing_spaces_impl {
struct Data {
    Face face;
};
}
using namespace overlay_trailing_spaces_impl;

static void overlay_trailing_spaces_start_frame(Editor*,
                                                Client*,
                                                const Buffer* buffer,
                                                Window_Unified* window,
                                                Contents_Iterator start_position_iterator,
                                                void* data) {}

static Face overlay_trailing_spaces_get_face_and_advance(
    const Buffer* buffer,
    Window_Unified* window,
    Contents_Iterator current_position_iterator,
    void* _data) {
    Data* data = (Data*)_data;

    if (!cz::is_blank(current_position_iterator.get())) {
        return {};
    }

    while (1) {
        for (size_t i = 0; i < window->cursors.len(); ++i) {
            if (current_position_iterator.position == window->cursors[i].point) {
                return {};
            }
            if (window->show_marks &&
                current_position_iterator.position >= window->cursors[i].start() &&
                current_position_iterator.position < window->cursors[i].end()) {
                return {};
            }
        }

        if (current_position_iterator.at_eob() || current_position_iterator.get() == '\n') {
            return data->face;
        }
        if (!cz::is_blank(current_position_iterator.get())) {
            return {};
        }
        current_position_iterator.advance();
    }
}

static Face overlay_trailing_spaces_get_face_newline_padding(const Buffer* buffer,
                                                             Window_Unified* window,
                                                             Contents_Iterator end_of_line_iterator,
                                                             void* data) {
    return {};
}

static void overlay_trailing_spaces_end_frame(void* data) {}

static void overlay_trailing_spaces_cleanup(void* data) {
    cz::heap_allocator().dealloc((Data*)data);
}

Overlay overlay_trailing_spaces(Face face) {
    static const Overlay::VTable vtable = {
        overlay_trailing_spaces_start_frame,
        overlay_trailing_spaces_get_face_and_advance,
        overlay_trailing_spaces_get_face_newline_padding,
        overlay_trailing_spaces_end_frame,
        overlay_trailing_spaces_cleanup,
    };

    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    *data = {};
    data->face = face;
    return {&vtable, data};
}

}
}
