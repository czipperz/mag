#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mag {
struct Window;

struct Screen_Position {
    size_t in_x;
    size_t in_y;

    bool found_window = false;
    bool found_position = false;
    Window_Unified* out_window;
    uint64_t out_position;
};

}
