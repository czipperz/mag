#include "overlay_trailing_spaces.hpp"

#include <ctype.h>
#include "overlay.hpp"

namespace mag {
namespace syntax {

struct Data {
    Face face;
};

static void overlay_trailing_spaces_start_frame(Buffer* buffer,
                                                Window_Unified* window,
                                                Contents_Iterator start_position_iterator,
                                                void* data) {}

static Face overlay_trailing_spaces_get_face_and_advance(
    Buffer* buffer,
    Window_Unified* window,
    Contents_Iterator current_position_iterator,
    void* _data) {
    Data* data = (Data*)_data;

    if (!isblank(current_position_iterator.get())) {
        return {};
    }

    while (1) {
        if (current_position_iterator.at_eob() || current_position_iterator.get() == '\n') {
            return data->face;
        }
        if (!isblank(current_position_iterator.get())) {
            return {};
        }
        current_position_iterator.advance();
    }
}

static Face overlay_trailing_spaces_get_face_newline_padding(Buffer* buffer,
                                                             Window_Unified* window,
                                                             Contents_Iterator end_of_line_iterator,
                                                             void* data) {
    return {};
}

static void overlay_trailing_spaces_end_frame(void* data) {}

static void overlay_trailing_spaces_cleanup(void* data) {
    free(data);
}

Overlay overlay_trailing_spaces() {
    static const Overlay::VTable vtable = {
        overlay_trailing_spaces_start_frame,
        overlay_trailing_spaces_get_face_and_advance,
        overlay_trailing_spaces_get_face_newline_padding,
        overlay_trailing_spaces_end_frame,
        overlay_trailing_spaces_cleanup,
    };

    Data* data = (Data*)calloc(1, sizeof(Data));
    CZ_ASSERT(data);
    data->face = {-1, 208, 0};
    return {&vtable, data};
}

}
}
