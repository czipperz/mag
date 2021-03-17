#pragma once

#include <atomic>
#include <cz/semaphore.hpp>
#include "buffer.hpp"

#ifdef NDEBUG
#include <cz/assert.hpp>
#endif

namespace mag {

class Buffer_Handle {
    cz::Semaphore semaphore;
    std::atomic_uint32_t starting_readers;
    std::atomic_uint32_t active_readers;
    std::atomic_uint32_t pending_writers;
    Buffer buffer;

public:
    Buffer_Id id;

    /// Call this with `buffer.directory`, `buffer.file`, and `buffer.is_temp` set.
    void init(Buffer_Id buffer_id, Buffer buffer) {
        semaphore.init(/*initial_value=*/1);

        id = buffer_id;

        this->buffer = buffer;
        this->buffer.init();
    }

    /// Lock the buffer for the purposes of reading and writing.
    /// Stalls until exclusive access can be obtained.
    Buffer* lock_writing();

    /// Lock the buffer for the purposes of reading.  Stalls until access can be obtained.
    ///
    /// This does *not* wait for pending writers (see
    /// `lock_writing`) unlike calls to `try_lock_reading`.
    ///
    /// Once `lock_reading` locks the buffer other readers will be allowed to
    /// use the buffer.  Note that if those other readers use `try_lock_reading`
    /// to gain access then they will still have to wait for pending writers.
    const Buffer* lock_reading();

    /// Lock the buffer for the purposes of reading, or return promptly with `nullptr` for failure.
    ///
    /// If the buffer is locked for writing, or there are one or more pending writers (via
    /// `lock_writing`), or there are many spurious errors then `nullptr` is returned.
    ///
    /// Multiple readers may use the buffer at the same time.
    const Buffer* try_lock_reading();

    /// Reduce the permissions of the lock from that of writing to only reading.
    void reduce_writing_to_reading();

    /// Unlock the buffer.
    ///
    /// Note: If this thread is a reader then this only actually unlocks
    /// the buffer if there are no other remaining readers.
    void unlock();

    void drop() {
        buffer.drop();
        semaphore.drop();
    }
};

}
