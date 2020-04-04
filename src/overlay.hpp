#pragma once

#include <stddef.h>

namespace mag {

struct Face;
struct Buffer;
struct Window_Unified;
struct Contents_Iterator;

struct Overlay {
    void* (*start_frame)(Buffer*, Window_Unified*, Contents_Iterator start_position_iterator);
    Face (*get_face_and_advance)(Buffer*,
                                 Window_Unified*,
                                 Contents_Iterator current_position_iterator,
                                 void*);
    void (*cleanup_frame)(void*);
};

}
