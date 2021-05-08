#include "overlay_preferred_column.hpp"

#include <cz/heap.hpp>
#include "buffer.hpp"
#include "movement.hpp"
#include "overlay.hpp"
#include "theme.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

namespace overlay_preferred_column_impl {
struct Data {
    Face face;
    uint64_t tab_width;
    uint64_t preferred_column;
    uint64_t column;
};
}
using namespace overlay_preferred_column_impl;

static void overlay_preferred_column_start_frame(const Buffer* buffer,
                                                 Window_Unified* window,
                                                 Contents_Iterator start_position_iterator,
                                                 void* _data) {
    Data* data = (Data*)_data;
    data->column = 0;
}

static Face overlay_preferred_column_get_face_and_advance(const Buffer* buffer,
                                                          Window_Unified* window,
                                                          Contents_Iterator iterator,
                                                          void* _data) {
    Data* data = (Data*)_data;
    Face face = {};
    char ch = iterator.get();
    if (ch == '\n') {
        data->column = 0;
    } else {
        uint64_t offset = 1;
        if (ch == '\t') {
            offset = buffer->mode.tab_width - (data->column % buffer->mode.tab_width);
        }
        if (data->column <= buffer->mode.preferred_column &&
            data->column + offset > buffer->mode.preferred_column) {
            face = data->face;
        }
        data->column += offset;
    }
    return face;
}

static Face overlay_preferred_column_get_face_newline_padding(const Buffer* buffer,
                                                              Window_Unified* window,
                                                              Contents_Iterator iterator,
                                                              void* _data) {
    return {};
}

static void overlay_preferred_column_end_frame(void* data) {}

static void overlay_preferred_column_cleanup(void* data) {
    cz::heap_allocator().dealloc((Data*)data);
}

Overlay overlay_preferred_column(Face face) {
    static const Overlay::VTable vtable = {
        overlay_preferred_column_start_frame,
        overlay_preferred_column_get_face_and_advance,
        overlay_preferred_column_get_face_newline_padding,
        overlay_preferred_column_end_frame,
        overlay_preferred_column_cleanup,
    };

    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    data->face = face;
    return {&vtable, data};
}

}
}
