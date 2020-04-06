#pragma once

#include <stddef.h>
#include "contents.hpp"
#include "theme.hpp"

namespace mag {

struct Buffer;
struct Window_Unified;

struct Overlay {
    struct VTable {
        void (*start_frame)(Buffer*,
                            Window_Unified*,
                            Contents_Iterator start_position_iterator,
                            void*);
        Face (*get_face_and_advance)(Buffer*,
                                     Window_Unified*,
                                     Contents_Iterator current_position_iterator,
                                     void*);
        void (*end_frame)(void*);
        void (*cleanup)(void*);
    };

    const VTable* vtable;
    void* data;

    void start_frame(Buffer* buffer,
                     Window_Unified* window,
                     Contents_Iterator start_position_iterator) {
        vtable->start_frame(buffer, window, start_position_iterator, data);
    }

    Face get_face_and_advance(Buffer* buffer, Window_Unified* window, Contents_Iterator iterator) {
        return vtable->get_face_and_advance(buffer, window, iterator, data);
    }

    void end_frame() { vtable->end_frame(data); }

    void cleanup() { vtable->end_frame(data); }
};

}
