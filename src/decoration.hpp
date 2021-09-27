#pragma once

#include <cz/heap_string.hpp>

namespace mag {
struct Buffer;
struct Client;
struct Editor;
struct Window_Unified;

struct Decoration {
    struct VTable {
        bool (*append)(Editor* editor,
                       Client* client,
                       const Buffer* buffer,
                       Window_Unified* window,
                       cz::Allocator allocator,
                       cz::String* string,
                       void* data);
        void (*cleanup)(void*);
    };

    const VTable* vtable;
    void* data;

    bool append(Editor* editor,
                Client* client,
                const Buffer* buffer,
                Window_Unified* window,
                cz::Allocator allocator,
                cz::String* string) const {
        return vtable->append(editor, client, buffer, window, allocator, string, data);
    }

    void cleanup() { vtable->cleanup(data); }
};

}
