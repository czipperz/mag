#include "overlay_matching_tokens.hpp"

#include <Tracy.hpp>
#include "basic/completion_commands.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "overlay.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

namespace overlay_nearest_matching_identifier_impl {
struct Data {
    Face face;

    uint64_t cache_cursor_position;
    uint64_t cache_change_index;

    uint64_t start;
    uint64_t end;
};
}
using namespace overlay_nearest_matching_identifier_impl;

static void overlay_nearest_matching_identifier_start_frame(Editor* editor,
                                                            Client* client,
                                                            const Buffer* buffer,
                                                            Window_Unified* window,
                                                            Contents_Iterator start,
                                                            void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;

    // Don't show completion if file is saved.  This cuts down on white noise while browsing.
    if (buffer->is_unchanged()) {
        data->start = 0;
        data->end = 0;
    }

    if (window->cursors[0].point == data->cache_cursor_position &&
        buffer->changes.len == data->cache_change_index) {
        return;
    }

    data->cache_cursor_position = window->cursors[0].point;
    data->cache_change_index = buffer->changes.len;

    data->start = 0;
    data->end = 0;

    start.go_to(window->cursors[0].point);
    Contents_Iterator middle = start;
    backward_through_identifier(&start);
    Contents_Iterator end = start;
    forward_through_identifier(&end);

    if (start.position >= middle.position) {
        return;
    }

    Contents_Iterator it;
    if (basic::find_nearest_matching_identifier(start, middle, end.position, /*max_buckets=*/5,
                                                &it)) {
        data->start = it.position;
        forward_through_identifier(&it);
        data->end = it.position;
    }
}

static Face overlay_nearest_matching_identifier_get_face_and_advance(const Buffer* buffer,
                                                                     Window_Unified* window,
                                                                     Contents_Iterator iterator,
                                                                     void* _data) {
    Data* data = (Data*)_data;

    if (iterator.position >= data->start && iterator.position < data->end) {
        return data->face;
    }
    return {};
}

static Face overlay_nearest_matching_identifier_get_face_newline_padding(const Buffer* buffer,
                                                                         Window_Unified* window,
                                                                         Contents_Iterator iterator,
                                                                         void* _data) {
    return {};
}

static void overlay_nearest_matching_identifier_end_frame(void* _data) {}

static void overlay_nearest_matching_identifier_cleanup(void* data) {
    cz::heap_allocator().dealloc((Data*)data);
}

Overlay overlay_nearest_matching_identifier(Face face) {
    static const Overlay::VTable vtable = {
        overlay_nearest_matching_identifier_start_frame,
        overlay_nearest_matching_identifier_get_face_and_advance,
        overlay_nearest_matching_identifier_get_face_newline_padding,
        overlay_nearest_matching_identifier_end_frame,
        overlay_nearest_matching_identifier_cleanup,
    };

    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    *data = {};
    data->face = face;
    return {&vtable, data};
}

}
}
