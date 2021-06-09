#pragma once

#include <cz/allocator.hpp>
#include <cz/buffer_array.hpp>
#include <cz/vector.hpp>
#include "command.hpp"
#include "ssostr.hpp"

namespace mag {

struct Buffer;
struct Edit;

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
    void commit(Command_Function committer = nullptr);
};

}
