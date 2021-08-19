#pragma once

#include <cz/allocator.hpp>
#include <cz/buffer_array.hpp>
#include <cz/vector.hpp>
#include "command.hpp"
#include "ssostr.hpp"

namespace mag {

struct Buffer;
struct Edit;
struct Client;
struct Asynchronous_Job_Handler;

/// A builder for a `Commit`.  The .  The `Buffer` must remain
/// locked during the lifetime of the `Transaction`!
struct Transaction {
    Buffer* buffer;
    cz::Buffer_Array::Save_Point save_point;
    cz::Vector<Edit> edits;
    bool committed;

    /// Initialize the transaction.
    void init(Buffer* buffer);

    /// If the transaction was not successfully committed (or `commit`
    /// was not called) then deallocates the associated memory.
    void drop();

    cz::Allocator value_allocator();

    void push(Edit edit);
    SSOStr last_edit_value() const;

    /// Commit the changes to the buffer.
    bool commit(Client* client, Command_Function committer = nullptr);
    bool commit(Asynchronous_Job_Handler* handler, Command_Function committer = nullptr);
    const char* commit_get_message(Command_Function committer = nullptr);
};

}
