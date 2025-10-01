#include "overlay_selected_line.hpp"

#include <cz/heap.hpp>
#include "core/buffer.hpp"
#include "core/movement.hpp"
#include "core/overlay.hpp"
#include "core/theme.hpp"
#include "core/window.hpp"

namespace mag {
namespace syntax {

namespace overlay_selected_line_impl {
struct Data {
    Face face;
    uint64_t start;
    uint64_t end;
};
}
using namespace overlay_selected_line_impl;

static void overlay_selected_line_start_frame(Editor*,
                                              Client*,
                                              const Buffer* buffer,
                                              Window_Unified* window,
                                              Contents_Iterator start_position_iterator,
                                              void* _data) {
    Data* data = (Data*)_data;
    data->start = 0;
    data->end = 0;
    if (window->cursors.len == 1) {
        uint64_t point = window->cursors[window->selected_cursor].point;
        if (point >= start_position_iterator.position) {
            Contents_Iterator start = start_position_iterator;
            if (!start.at_eob()) {
                start.advance_to(point);
            }
            Contents_Iterator end = start;
            start_of_line(&start);
            end_of_line(&end);
            data->start = start.position;
            data->end = end.position;
        }
    }
}

static Face overlay_selected_line_get_face_and_advance(const Buffer* buffer,
                                                       Window_Unified* window,
                                                       Contents_Iterator iterator,
                                                       void* _data) {
    Data* data = (Data*)_data;
    Face face = {};
    if (iterator.position >= data->start && iterator.position <= data->end) {
        face = data->face;
    }
    return face;
}

static void overlay_selected_line_skip_forward_same_line(const Buffer*,
                                                         Window_Unified*,
                                                         Contents_Iterator start,
                                                         uint64_t end,
                                                         void* _data) {}

static void overlay_selected_line_end_frame(void* data) {}

static void overlay_selected_line_cleanup(void* data) {
    cz::heap_allocator().dealloc((Data*)data);
}

Overlay overlay_selected_line(Face face) {
    static const Overlay::VTable vtable = {
        overlay_selected_line_start_frame,
        overlay_selected_line_get_face_and_advance,
        overlay_selected_line_get_face_and_advance,  // newline padding = normal line text
        overlay_selected_line_skip_forward_same_line,
        overlay_selected_line_end_frame,
        overlay_selected_line_cleanup,
    };

    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    data->face = face;
    return {&vtable, data};
}

}
}
