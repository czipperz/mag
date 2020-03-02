#include "transaction.hpp"

#include "buffer.hpp"
#include "commit.hpp"

namespace mag {

void Transaction::commit(Buffer* buffer) {
    Commit commit;
    commit.edits = buffer->commit_buffer.allocator().duplicate(edits.as_slice());

    buffer->commit(commit);
}

}
