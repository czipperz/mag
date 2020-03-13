#include "transaction.hpp"

#include "buffer.hpp"
#include "commit.hpp"

namespace mag {

void Transaction::init(size_t num_edits, size_t total_edit_values) {
    edit_offset = 0;
    value_offset = num_edits * sizeof(Edit);
    memory = malloc(value_offset + total_edit_values);
}

void Transaction::drop() {
    free(memory);
}

static cz::MemSlice transaction_alloc(void* data, cz::AllocInfo new_info) {
    Transaction* transaction = (Transaction*)data;
    CZ_DEBUG_ASSERT(new_info.alignment == 1);
    cz::MemSlice slice = {(char*)transaction->memory + transaction->value_offset, new_info.size};
    transaction->value_offset += new_info.size;
    return slice;
}

static void transaction_dealloc(void* data, cz::MemSlice old_mem) {
    CZ_PANIC("");
}

static cz::MemSlice transaction_realloc(void* data, cz::MemSlice old_mem, cz::AllocInfo new_info) {
    CZ_PANIC("");
}

static const cz::Allocator::VTable allocator_vtable = {transaction_alloc, transaction_dealloc,
                                                       transaction_realloc};

cz::Allocator Transaction::value_allocator() {
    return {&allocator_vtable, this};
}

void Transaction::push(Edit edit) {
    memcpy((char*)memory + edit_offset, &edit, sizeof(Edit));
    edit_offset += sizeof(Edit);
}

cz::Str Transaction::last_edit_value() const {
    void* ptr = (char*)memory + edit_offset - sizeof(Edit);
    return ((Edit*)ptr)->value.as_str();
}

void Transaction::commit(Buffer* buffer) {
    Commit commit;
    commit.edits = {(Edit*)memory, edit_offset / sizeof(Edit)};
    buffer->commit(commit);
    memory = nullptr;
}

}
