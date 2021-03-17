#include "buffer_handle.hpp"

#include <cz/assert.hpp>
#include <cz/defer.hpp>

namespace mag {

static bool join_readers(std::atomic_uint32_t& readers) {
    uint32_t count = readers.load();
    if (count > 0) {
        // Increment the number of readers only if it hasn't changed.
        if (readers.compare_exchange_weak(count, count + 1)) {
            return true;
        }
    }
    return false;
}

Buffer* Buffer_Handle::lock_writing() {
    pending_writers.fetch_add(1);
    CZ_DEFER(pending_writers.fetch_sub(1));

    semaphore.acquire();

    return &buffer;
}

const Buffer* Buffer_Handle::lock_reading() {
    {
        // Prevent `try_lock_reading` readers from acquiring
        // the lock because that causes a race condition.
        pending_writers.fetch_add(1);
        CZ_DEFER(pending_writers.fetch_sub(1));

        // If there are other active readers then join them.
        if (join_readers(starting_readers)) {
            active_readers.fetch_add(1);
            return &buffer;
        }

        // Forcibly obtain the lock.  Note that this can stall and not merge
        // with another active reader if a different reader locks the buffer
        // after we return from `try_lock_reading` and before this line.
        semaphore.acquire();
    }

    // Let other readers in.  Note they won't succeed at locking unless there are no writers.
    uint32_t sr = starting_readers.fetch_add(1);
    CZ_DEBUG_ASSERT(sr == 0);
    uint32_t ar = active_readers.fetch_add(1);
    CZ_DEBUG_ASSERT(ar == 0);

    return &buffer;
}

const Buffer* Buffer_Handle::try_lock_reading() {
    for (int attempts = 0;; ++attempts) {
        // Limit the number of attempts just in case we
        // can't make progress due to spurious failures.
        if (attempts == 8) {
            return nullptr;
        }

        // Prioritize writers over readers.
        if (pending_writers.load() > 0) {
            return nullptr;
        }

        // If there are already active readers then we can join them.
        if (join_readers(active_readers)) {
            starting_readers.fetch_add(1);
            return &buffer;
        }

        // Try to lock.  This may spuriously fail.
        bool locked = semaphore.try_acquire();
        if (locked) {
            // If we succeed then mark that we're reading for the `lock_reading` function.
            uint32_t sr = starting_readers.fetch_add(1);
            CZ_DEBUG_ASSERT(sr == 0);

            // Unlock after we increment to prevent a race condition with `lock_reading`.
            if (pending_writers.load() > 0) {
                starting_readers.fetch_sub(1);
                semaphore.release();
                return nullptr;
            }

            // If we succeed then mark that we're reading for other `try_lock_reading` calls.
            // Note that we may not be the first active reader because `lock_reading` may
            // have pre-empted us and taken that position.  But that's fine.
            active_readers.fetch_add(1);

            return &buffer;
        }
    }
}

void Buffer_Handle::unlock() {
    uint32_t ar = active_readers.load();

    // If there are active readers then this thread is a reader.
    if (ar >= 1) {
        starting_readers.fetch_sub(1);
        active_readers.fetch_sub(1);
    }

    // Unlock if either this thread is the last active reader
    // (`ar == 1`) or this thread is a writer (`ar == 0`).
    if (ar <= 1) {
        semaphore.release();
    }
}

}
