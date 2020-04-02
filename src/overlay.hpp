#pragma once

#include <stddef.h>

namespace mag {

struct Face;
struct Buffer;
struct Window_Unified;

struct Overlay {
    void* (*start_frame)(Buffer*, Window_Unified*);
    Face (*get_face_and_advance)(Buffer*, Window_Unified*, void*);
    void (*cleanup_frame)(void*);
};

}
