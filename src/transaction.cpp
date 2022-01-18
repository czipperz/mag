#include "transaction.hpp"

#include <cz/debug.hpp>
#include <cz/heap.hpp>
#include "buffer.hpp"
#include "client.hpp"
#include "commit.hpp"
#include "job.hpp"

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
    if (edit.value.len() == 0)
        return;
    edits.reserve(cz::heap_allocator(), 1);
    edits.push(edit);
}

SSOStr Transaction::last_edit_value() const {
    return edits.last().value;
}

bool Transaction::commit(Client* client, Command_Function committer) {
    const char* message = commit_get_message(committer);
    if (message) {
        client->show_message(message);
    }
    return message == nullptr;
}
bool Transaction::commit(Asynchronous_Job_Handler* handler, Command_Function committer) {
    const char* message = commit_get_message(committer);
    if (message) {
        handler->show_message(message);
    }
    return message == nullptr;
}

const char* Transaction::commit_get_message(Command_Function committer) {
    // Only commit if edits were made.
    if (edits.len == 0) {
        return nullptr;
    }

    if (buffer->read_only) {
        return "Buffer is read only";
    }

    auto edits_clone = edits.clone(buffer->commit_buffer_array.allocator());
    if (!buffer->commit(edits_clone, committer)) {
        cz::dbreak();
        return "Error: invalid edit";
    }

    committed = true;
    return nullptr;
}

}
