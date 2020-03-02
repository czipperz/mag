#pragma once

#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include "edit.hpp"

namespace mag {

struct Buffer;

struct Transaction {
    cz::Vector<Edit> edits;

    void reserve(size_t num) { edits.reserve(cz::heap_allocator(), num); }

    void push(Edit edit) { edits.push(edit); }

    void drop() { edits.drop(cz::heap_allocator()); }

    void commit(Buffer* buffer);
};

}
