#include "overlay_preferred_column.hpp"

#include "buffer.hpp"
#include "movement.hpp"
#include "overlay.hpp"
#include "theme.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

struct Data {
    uint64_t column;
};

static void* overlay_preferred_column_start_frame(Buffer* buffer,
                                                  Window_Unified* window,
                                                  Contents_Iterator start_position_iterator) {
    Data* data = (Data*)malloc(sizeof(Data));
    data->column = 0;
    return data;
}

static Face overlay_preferred_column_get_face_and_advance(Buffer* buffer,
                                                          Window_Unified* window,
                                                          Contents_Iterator iterator,
                                                          void* _data) {
    Data* data = (Data*)_data;
    Face face = {};
    if (data->column == 100) {
        face = {-1, 21, 0};
    }

    if (iterator.get() == '\n') {
        data->column = 0;
    }
    ++data->column;
    return face;
}

static void overlay_preferred_column_cleanup_frame(void* data) {
    free(data);
}

Overlay overlay_preferred_column() {
    return Overlay{
        overlay_preferred_column_start_frame,
        overlay_preferred_column_get_face_and_advance,
        overlay_preferred_column_cleanup_frame,
    };
}

}
}
