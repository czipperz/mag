#include "transaction.hpp"

#include <cz/heap.hpp>
#include "buffer.hpp"
#include "commit.hpp"

namespace mag {

void Transaction::init(Buffer* buffer_) {
    edits = {};
    buffer = buffer_;
    save_point = buffer->commit_buffer_array.save();
    committed = false;
}

void Transaction::drop() {
    edits.drop(cz::heap_allocator());
    if (!committed) {
        buffer->commit_buffer_array.restore(save_point);
    }
}

cz::Allocator Transaction::value_allocator() {
    return buffer->commit_buffer_array.allocator();
}

void Transaction::push(Edit edit) {
    edits.reserve(cz::heap_allocator(), 1);
    edits.push(edit);
}

SSOStr Transaction::last_edit_value() const {
    return edits.last().value;
}

void Transaction::commit(Command_Function committer) {
    // Only commit if edits were made.
    if (edits.len() > 0) {
        if (buffer->commit(edits.clone(buffer->commit_buffer_array.allocator()), committer)) {
            committed = true;
        }
    }
}

}
