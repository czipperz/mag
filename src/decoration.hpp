#pragma once

#include <cz/string.hpp>

namespace mag {
struct Buffer;
struct Window_Unified;

struct Decoration {
    struct VTable {
        bool (*append)(const Buffer*, Window_Unified*, cz::AllocatedString*, void*);
        void (*cleanup)(void*);
    };

    const VTable* vtable;
    void* data;

    bool append(const Buffer* buffer, Window_Unified* window, cz::AllocatedString* string) {
        return vtable->append(buffer, window, string, data);
    }

    void cleanup() { vtable->cleanup(data); }
};

}
