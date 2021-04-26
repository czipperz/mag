#pragma once

#include <cz/heap_string.hpp>

namespace mag {
struct Buffer;
struct Window_Unified;

struct Decoration {
    struct VTable {
        bool (*append)(const Buffer*, Window_Unified*, cz::Allocator, cz::String*, void*);
        void (*cleanup)(void*);
    };

    const VTable* vtable;
    void* data;

    bool append(const Buffer* buffer,
                Window_Unified* window,
                cz::Allocator allocator,
                cz::String* string) const {
        return vtable->append(buffer, window, allocator, string, data);
    }

    void cleanup() { vtable->cleanup(data); }
};

}
