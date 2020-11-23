#include "overlay_matching_tokens.hpp"

#include <stdint.h>
#include <stdlib.h>
#include <Tracy.hpp>
#include "buffer.hpp"
#include "overlay.hpp"
#include "theme.hpp"
#include "token.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

struct Data {
    Face face;

    bool enabled;

    Contents_Iterator start_marked_region;

    size_t countdown_cursor_region;
};

static void overlay_matching_region_start_frame(Buffer* buffer,
                                                Window_Unified* window,
                                                Contents_Iterator start_position_iterator,
                                                void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;

    data->enabled = window->show_marks && window->cursors[0].point != window->cursors[0].mark;
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

static Face overlay_matching_region_get_face_and_advance(Buffer* buffer,
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
        Contents_Iterator marked_region_iterator = data->start_marked_region;
        uint64_t end_marked_region = window->cursors[0].end();
        while (marked_region_iterator.position < end_marked_region && !iterator.at_eob()) {
            if (marked_region_iterator.get() != iterator.get()) {
                break;
            }
            marked_region_iterator.advance();
            iterator.advance();
        }

        if (marked_region_iterator.position == end_marked_region) {
            data->countdown_cursor_region = end_marked_region - data->start_marked_region.position;
        }
    }

    if (data->countdown_cursor_region > 0) {
        return data->face;
    } else {
        return {};
    }
}

static Face overlay_matching_region_get_face_newline_padding(Buffer* buffer,
                                                             Window_Unified* window,
                                                             Contents_Iterator iterator,
                                                             void* _data) {
    return {};
}

static void overlay_matching_region_end_frame(void* _data) {}

static void overlay_matching_region_cleanup(void* data) {
    free(data);
}

Overlay overlay_matching_region(Face face) {
    static const Overlay::VTable vtable = {
        overlay_matching_region_start_frame,
        overlay_matching_region_get_face_and_advance,
        overlay_matching_region_get_face_newline_padding,
        overlay_matching_region_end_frame,
        overlay_matching_region_cleanup,
    };

    Data* data = (Data*)malloc(sizeof(Data));
    CZ_ASSERT(data);
    data->face = face;
    return {&vtable, data};
}

}
}
