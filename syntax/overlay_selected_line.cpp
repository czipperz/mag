#include "overlay_selected_line.hpp"

#include "buffer.hpp"
#include "movement.hpp"
#include "overlay.hpp"
#include "theme.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

struct Data {
    Contents_Iterator iterator;
    uint64_t start;
    uint64_t end;
};

static void* overlay_selected_line_start_frame(Buffer* buffer,
                                               Window_Unified* window,
                                               Contents_Iterator start_position_iterator) {
    Data* data = (Data*)malloc(sizeof(Data));
    data->start = 0;
    data->end = 0;
    if (window->cursors.len() == 1) {
        uint64_t point = window->cursors[0].point;
        if (point >= start_position_iterator.position) {
            Contents_Iterator start = start_position_iterator;
            start.advance_to(point);
            Contents_Iterator end = start;
            start_of_line(&start);
            end_of_line(&end);
            data->start = start.position;
            data->end = end.position;
        }
    }
    return data;
}

static Face overlay_selected_line_get_face_and_advance(Buffer* buffer,
                                                       Window_Unified* window,
                                                       Contents_Iterator iterator,
                                                       void* _data) {
    Data* data = (Data*)_data;
    Face face = {};
    if (iterator.position >= data->start && iterator.position < data->end) {
        face = {-1, 21, 0};
    }
    iterator.advance();
    return face;
}

static void overlay_selected_line_cleanup_frame(void* data) {
    free(data);
}

Overlay overlay_selected_line() {
    return Overlay{
        overlay_selected_line_start_frame,
        overlay_selected_line_get_face_and_advance,
        overlay_selected_line_cleanup_frame,
    };
}

}
}
