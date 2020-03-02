#pragma once

#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include "buffer_handle.hpp"
#include "buffer_id.hpp"
#include "key_map.hpp"

namespace mag {

struct Editor {
    cz::Vector<Buffer_Handle> buffers;
    Key_Map key_map;

    void drop() {
        for (size_t i = 0; i < buffers.len(); ++i) {
            buffers[i].drop();
        }
        buffers.drop(cz::heap_allocator());
        key_map.drop();
    }

    Buffer_Handle* lookup(Buffer_Id id) { return &buffers[id.value]; }

    void create_buffer(cz::Str name) {
        Buffer_Handle buffer = {};
        buffer.init({buffers.len()}, name);
        buffers.reserve(cz::heap_allocator(), 1);
        buffers.push(buffer);
    }
};

}
