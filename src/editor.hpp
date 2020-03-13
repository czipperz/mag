#pragma once

#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include "buffer_handle.hpp"
#include "buffer_id.hpp"
#include "key_map.hpp"
#include "theme.hpp"

namespace mag {

struct Editor {
    cz::Vector<Buffer_Handle*> buffers;

    Key_Map key_map;
    Theme theme;

    void drop() {
        for (size_t i = 0; i < buffers.len(); ++i) {
            buffers[i]->drop();
            cz::heap_allocator().dealloc({buffers[i], sizeof(Buffer_Handle)});
        }
        buffers.drop(cz::heap_allocator());
        key_map.drop();
        theme.drop(cz::heap_allocator());
    }

    Buffer_Handle* lookup(Buffer_Id id) { return buffers[id.value]; }

    Buffer_Id create_buffer(cz::Str path) {
        Buffer_Handle* buffer_handle = cz::heap_allocator().create<Buffer_Handle>();
        buffer_handle->init({buffers.len()}, path);
        buffers.reserve(cz::heap_allocator(), 1);
        buffers.push(buffer_handle);
        return {buffers.len() - 1};
    }
};

}
