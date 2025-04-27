#include "overlay_indent_guides.hpp"

#include <stdlib.h>
#include <algorithm>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/sort.hpp>
#include <cz/vector.hpp>
#include <tracy/Tracy.hpp>
#include "buffer.hpp"
#include "movement.hpp"
#include "overlay.hpp"
#include "theme.hpp"
#include "token.hpp"
#include "visible_region.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

namespace overlay_indent_guides_impl {
struct Data {
    Face face;
    cz::Vector<uint64_t> columns;
    size_t index;
    uint64_t column;
    bool has_saved_column;
    uint64_t saved_column;
};
}
using namespace overlay_indent_guides_impl;

static void overlay_indent_guides_start_frame(Editor*,
                                              Client*,
                                              const Buffer*,
                                              Window_Unified*,
                                              Contents_Iterator start_position_iterator,
                                              void* _data) {
    Data* data = (Data*)_data;
    data->columns.len = 0;
    data->column = 0;
    data->index = 0;
    data->has_saved_column = false;
}

static Face overlay_indent_guides_get_face_and_advance(const Buffer*,
                                                       Window_Unified*,
                                                       Contents_Iterator current_position_iterator,
                                                       void* _data) {
    Data* data = (Data*)_data;
    if (current_position_iterator.get() == '\n') {
        data->column = 0;
        data->index = 0;
        if (data->has_saved_column) {
            data->columns.reserve(cz::heap_allocator(), 1);
            data->columns.push(data->saved_column);
        }
        data->has_saved_column = false;
        return {};
    }

    if (data->column == 0) {
        Contents_Iterator end = current_position_iterator;
        forward_through_whitespace(&end);
        uint64_t column = end.position - current_position_iterator.position;
        size_t i;
        for (i = 0; i < data->columns.len; ++i) {
            if (column <= data->columns[i]) {
                break;
            }
        }
        data->columns.len = i;
        if (data->columns.len == 0 || data->columns.last() < column) {
            data->has_saved_column = true;
            data->saved_column = column;
        }
    }

    Face face = {};
    for (size_t i = 0; i < data->columns.len; ++i) {
        if (data->columns[i] == data->column) {
            face = data->face;
            break;
        }
    }
    ++data->column;
    return face;
}

static Face overlay_indent_guides_get_face_newline_padding(const Buffer*,
                                                           Window_Unified*,
                                                           Contents_Iterator end_of_line_iterator,
                                                           void*) {
    return {};
}

static void overlay_indent_guides_end_frame(void* data) {}

static void overlay_indent_guides_cleanup(void* _data) {
    Data* data = (Data*)_data;
    data->columns.drop(cz::heap_allocator());
    cz::heap_allocator().dealloc(data);
}

Overlay overlay_indent_guides(Face face) {
    static const Overlay::VTable vtable = {
        overlay_indent_guides_start_frame,
        overlay_indent_guides_get_face_and_advance,
        overlay_indent_guides_get_face_newline_padding,
        overlay_indent_guides_end_frame,
        overlay_indent_guides_cleanup,
    };

    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    *data = {};
    data->face = face;
    return {&vtable, data};
}

}
}
