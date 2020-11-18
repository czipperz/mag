#pragma once

#include <stddef.h>
#include <stdint.h>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include "buffer_id.hpp"

namespace mag {
struct Buffer;

struct Jump {
    Buffer_Id buffer_id;
    uint64_t position;
    size_t change_index;

    void update(Buffer* buffer);
};

struct Jump_Chain {
    cz::Vector<Jump> jumps;
    size_t index;

    void push(Jump jump) {
        jumps.set_len(index);
        jumps.reserve(cz::heap_allocator(), 1);
        jumps.push(jump);
        ++index;
    }

    Jump* pop() {
        if (index > 0) {
            return &jumps[--index];
        } else {
            return nullptr;
        }
    }

    Jump* unpop() {
        if (index + 1 < jumps.len()) {
            return &jumps[++index];
        } else {
            return nullptr;
        }
    }

    void drop() { jumps.drop(cz::heap_allocator()); }
};

struct Window_Unified;
struct Client;
struct Buffer;
struct Editor;
void push_jump(Window_Unified* window, Client* client, Buffer_Id buffer_id, Buffer* buffer);
void goto_jump(Editor* editor, Client* client, Jump* jump);

}
