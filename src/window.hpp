#pragma once

#include "buffer_id.hpp"

namespace mag {

struct Window {
    Window* parent;

    enum Tag {
        UNIFIED,
        VERTICAL_SPLIT,
        HORIZONTAL_SPLIT,
    } tag;
    union {
        Buffer_Id unified_id;
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
