#pragma once

#include <cz/allocator.hpp>

namespace mag {

struct Buffer;
struct Edit;

struct Transaction {
    void* memory;
    size_t edit_offset;
    size_t value_offset;

    void init(size_t num_edits, size_t total_edit_values);

    void drop();

    cz::Allocator value_allocator();

    void push(Edit edit);

    void commit(Buffer* buffer);
};

}
