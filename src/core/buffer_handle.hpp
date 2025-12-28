#pragma once

#include <atomic>
#include <cz/condition_variable.hpp>
#include <cz/mutex.hpp>
#include "core/buffer.hpp"

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

namespace mag {

struct Buffer_Handle {
private:
    cz::Mutex mutex;
    cz::Condition_Variable waiters_condition;
    uint32_t waiters_count;
    uint32_t active_state;

#ifdef TRACY_ENABLE
    tracy::SharedLockableCtx* context;
#endif

#ifndef NDEBUG
    cz::Vector<uint64_t> associated_threads;
#endif

    Buffer buffer;

public:
    /// Call this with `buffer.directory`, `buffer.name`, and `buffer.type` set.
    void init(Buffer buffer);
    void drop();

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

    /// Increase the permissions of the lock from that
    /// of only reading to that of reading and writing.
    ///
    /// If there are other readers, this will stall until they `unlock`.  Note that other
    /// writers may preempt this function finishing, which may modify the state of the
    /// buffer.  Thus you should handle the possibility that the buffer changed while
    /// `increase_reading_to_writing` was stalled similarly to a compare and swap operation.
    Buffer* increase_reading_to_writing();

    /// Assumes `buffer` is controlled by a `cz::Arc<Buffer_Handle>`
    /// and reinterpret casts it back so the `cz::Arc` can be used.
    static cz::Arc<Buffer_Handle> cast_to_arc_handle_no_inc(const Buffer* buffer);

    /// Unlock the buffer.
    ///
    /// Note: If this thread is a reader then this only actually unlocks
    /// the buffer if there are no other remaining readers.
    void unlock();
};

cz::Arc<Buffer_Handle> create_buffer_handle(Buffer);

}
