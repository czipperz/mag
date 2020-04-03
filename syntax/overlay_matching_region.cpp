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

    Contents_Iterator iterator;
    Contents_Iterator start_marked_region;

    size_t countdown_cursor_region;
};

static void* overlay_matching_region_start_frame(Buffer* buffer, Window_Unified* window) {
    ZoneScoped;

    Data* data = (Data*)malloc(sizeof(Data));

    data->enabled = window->show_marks;
    if (!data->enabled) {
        return data;
    }

    data->face = {-1, 237, 0};
    data->iterator = buffer->contents.iterator_at(window->start_position);
    data->start_marked_region = data->iterator;
    if (data->start_marked_region.at_eob()) {
        data->enabled = false;
    } else {
        if (window->cursors[0].start() >= data->start_marked_region.position) {
            data->start_marked_region.advance(window->cursors[0].start() -
                                              data->start_marked_region.position);
        } else {
            data->enabled = false;
        }
    }
    data->countdown_cursor_region = 0;
    return data;
}

static Face overlay_matching_region_get_face_and_advance(Buffer* buffer,
                                                         Window_Unified* window,
                                                         void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;

    if (!data->enabled) {
        return {};
    }

    if (data->countdown_cursor_region > 0) {
        --data->countdown_cursor_region;
    } else {
        Contents_Iterator marked_region_iterator = data->start_marked_region;
        Contents_Iterator it = data->iterator;
        uint64_t end_marked_region = window->cursors[0].end();
        while (marked_region_iterator.position < end_marked_region && !it.at_eob()) {
            if (marked_region_iterator.get() != it.get()) {
                break;
            }
            marked_region_iterator.advance();
            it.advance();
        }

        if (marked_region_iterator.position == end_marked_region) {
            data->countdown_cursor_region = end_marked_region - data->start_marked_region.position;
        }
    }

    data->iterator.advance();

    if (data->countdown_cursor_region > 0) {
        return data->face;
    } else {
        return {};
    }
}

static void overlay_matching_region_cleanup_frame(void* data) {
    free(data);
}

Overlay overlay_matching_region() {
    return Overlay{
        overlay_matching_region_start_frame,
        overlay_matching_region_get_face_and_advance,
        overlay_matching_region_cleanup_frame,
    };
}

}
}
