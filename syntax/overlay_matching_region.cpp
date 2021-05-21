#include "overlay_matching_tokens.hpp"

#include <stdint.h>
#include <stdlib.h>
#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include <cz/heap.hpp>
#include "buffer.hpp"
#include "match.hpp"
#include "overlay.hpp"
#include "theme.hpp"
#include "token.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

namespace overlay_matching_region_impl {
struct Data {
    Face face;

    bool enabled;

    Contents_Iterator start_marked_region;

    size_t countdown_cursor_region;
};
}
using namespace overlay_matching_region_impl;

static void overlay_matching_region_start_frame(const Buffer* buffer,
                                                Window_Unified* window,
                                                Contents_Iterator start_position_iterator,
                                                void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;

    // Only enable if we've selected a region.
    data->enabled = window->show_marks && window->cursors[0].point != window->cursors[0].mark;

    // Disable if the region couldn't match anything else because it is
    // giant.  Comparing the massive string takes a long time.  This happens
    // when the user selects the entire file (ie `command_mark_buffer`).
    if (window->cursors[0].end() - window->cursors[0].start() > buffer->contents.len / 2) {
        data->enabled = false;
    }

    if (!data->enabled) {
        return;
    }

    data->start_marked_region = start_position_iterator;
    if (data->start_marked_region.at_eob()) {
        data->enabled = false;
    } else {
        if (window->cursors[0].start() >= data->start_marked_region.position) {
            data->start_marked_region.advance_to(window->cursors[0].start());
        } else {
            data->enabled = false;
        }
    }
    data->countdown_cursor_region = 0;
}

static Face overlay_matching_region_get_face_and_advance(const Buffer* buffer,
                                                         Window_Unified* window,
                                                         Contents_Iterator iterator,
                                                         void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;

    if (!data->enabled) {
        return {};
    }

    if (data->countdown_cursor_region > 0) {
        --data->countdown_cursor_region;
    }

    if (data->countdown_cursor_region == 0) {
        uint64_t end_marked_region = window->cursors[0].end();
        if (matches_cased(data->start_marked_region, end_marked_region, iterator,
                          buffer->mode.search_case_insensitive)) {
            data->countdown_cursor_region = end_marked_region - data->start_marked_region.position;
        }
    }

    if (data->countdown_cursor_region > 0) {
        return data->face;
    } else {
        return {};
    }
}

static Face overlay_matching_region_get_face_newline_padding(const Buffer* buffer,
                                                             Window_Unified* window,
                                                             Contents_Iterator iterator,
                                                             void* _data) {
    return {};
}

static void overlay_matching_region_end_frame(void* _data) {}

static void overlay_matching_region_cleanup(void* data) {
    cz::heap_allocator().dealloc((Data*)data);
}

Overlay overlay_matching_region(Face face) {
    static const Overlay::VTable vtable = {
        overlay_matching_region_start_frame,
        overlay_matching_region_get_face_and_advance,
        overlay_matching_region_get_face_newline_padding,
        overlay_matching_region_end_frame,
        overlay_matching_region_cleanup,
    };

    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    *data = {};
    data->face = face;
    return {&vtable, data};
}

}
}
