#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mag {
struct Window_Unified;

namespace render {
struct Screen_Position {
    bool found_window = false;
    bool found_position = false;
    Window_Unified* window;
    uint64_t position;
};

struct Screen_Position_Query {
    size_t in_x;
    size_t in_y;

    Screen_Position sp;

    int data; // used in clients to store what event this corresponds to
};

}
}
