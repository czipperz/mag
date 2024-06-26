#include "overlay_nearest_matching_identifier.hpp"

#include <cz/defer.hpp>
#include <tracy/Tracy.hpp>
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

    Contents_Iterator start;
    uint64_t end;

    uint64_t countdown;
    bool countdown_highlight;
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
    data->countdown = 0;
    data->countdown_highlight = false;

    // Don't show completion if file is saved.  This cuts down on white noise while browsing.
    if (buffer->is_unchanged() || window->show_marks) {
        data->start = {};
        data->end = 0;
        return;
    }

    if (window->cursors[window->selected_cursor].point == data->cache_cursor_position &&
        buffer->changes.len == data->cache_change_index) {
        return;
    }

    data->cache_cursor_position = window->cursors[window->selected_cursor].point;
    data->cache_change_index = buffer->changes.len;

    data->start = {};
    data->end = 0;

    start.go_to(window->cursors[window->selected_cursor].point);
    Contents_Iterator middle = start;
    backward_through_identifier(&start);

    if (start.position >= middle.position) {
        return;
    }

    cz::Vector<uint64_t> cursor_positions = {};
    CZ_DEFER(cursor_positions.drop(cz::heap_allocator()));
    cursor_positions.reserve_exact(cz::heap_allocator(), window->cursors.len);
    for (size_t i = 0; i < window->cursors.len; ++i) {
        cursor_positions.push(window->cursors[i].point);
    }

    Contents_Iterator it;
    if (basic::find_nearest_matching_identifier(start, middle, /*max_buckets=*/5,
                                                /*ignored_positions=*/cursor_positions, &it)) {
        data->start = it;
        forward_through_identifier(&it);
        data->end = it.position;
    }
}

static Face overlay_nearest_matching_identifier_get_face_and_advance(const Buffer* buffer,
                                                                     Window_Unified* window,
                                                                     Contents_Iterator iterator,
                                                                     void* _data) {
    Data* data = (Data*)_data;

    if (data->end == 0)
        return {};

    if (data->countdown == 0) {
        char first = iterator.get();
        if (!cz::is_alnum(first) && first != '_')
            return {};

        Contents_Iterator end = iterator;
        forward_through_identifier(&end);
        data->countdown = end.position - iterator.position;
        data->countdown_highlight = matches(data->start, data->end, iterator, end.position);
    }

    --data->countdown;
    if (data->countdown_highlight)
        return data->face;
    else
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
