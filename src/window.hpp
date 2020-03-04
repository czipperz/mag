#pragma once

#include <stddef.h>
#include <stdint.h>
#include "buffer_id.hpp"

namespace mag {

struct Window {
    Window* parent;

    size_t rows;
    size_t cols;

    enum Tag {
        UNIFIED,
        VERTICAL_SPLIT,
        HORIZONTAL_SPLIT,
    } tag;
    union {
        struct {
            Buffer_Id id;
            uint64_t start_position;
        } unified;
        struct {
            Window* left;
            Window* right;
        } vertical_split;
        struct {
            Window* top;
            Window* bottom;
        } horizontal_split;
    } v;

    static Window* alloc();
    static Window* create(Buffer_Id buffer_id);
    static void drop(Window* window);
};

}
