#include "buffer_handle.hpp"

#include <Tracy.hpp>
#include <cz/assert.hpp>
#include <cz/defer.hpp>

namespace mag {

enum : uint32_t {
    UNLOCKED = 0,
    LOCKED_WRITING = 1,
    READER_0 = 2,
};

void Buffer_Handle::init(Buffer_Id buffer_id, Buffer buffer) {
    mutex.init();
    waiters_condition.init();

    waiters_count = 0;
    active_state = UNLOCKED;

#ifdef TRACY_ENABLE
    context = new tracy::SharedLockableCtx([]() -> const tracy::SourceLocationData* {
        static constexpr tracy::SourceLocationData srcloc{nullptr, "cz::Buffer_Handle", __FILE__,
                                                          __LINE__, 0};
        return &srcloc;
    }());
    context->CustomName(buffer.name.buffer(), buffer.name.len());
#endif

    id = buffer_id;

    this->buffer = buffer;
    this->buffer.init();
}

void Buffer_Handle::drop() {
    buffer.drop();

    mutex.drop();
    waiters_condition.drop();

#ifdef TRACY_ENABLE
    delete context;
#endif
}

Buffer* Buffer_Handle::lock_writing() {
    ZoneScoped;

#ifdef TRACY_ENABLE
    const auto run_after = context->BeforeLock();
#endif

    {
        mutex.lock();
        CZ_DEFER(mutex.unlock());

        ++waiters_count;
        // Wait for exclusive access.
        while (active_state != UNLOCKED) {
            waiters_condition.wait(&mutex);
        }
        --waiters_count;

        active_state = LOCKED_WRITING;
    }

#ifdef TRACY_ENABLE
    if (run_after) {
        context->AfterLock();
    }
#endif

    return &buffer;
}

const Buffer* Buffer_Handle::lock_reading() {
    ZoneScoped;

#ifdef TRACY_ENABLE
    const auto run_after = context->BeforeLockShared();
#endif

    {
        mutex.lock();
        CZ_DEFER(mutex.unlock());

        ++waiters_count;
        // Wait until there is no active writer.
        while (active_state == LOCKED_WRITING) {
            waiters_condition.wait(&mutex);
        }
        --waiters_count;

        if (active_state == UNLOCKED) {
            // Lock in read mode.
            active_state = READER_0;

            // Wake up all waiters so other readers will also run.
            waiters_condition.signal_all();
        } else {
            // Already locked in read mode.
            CZ_DEBUG_ASSERT(active_state >= READER_0);
            ++active_state;
        }
    }

#ifdef TRACY_ENABLE
    if (run_after) {
        context->AfterLockShared();
    }
#endif

    return &buffer;
}

const Buffer* Buffer_Handle::try_lock_reading() {
    ZoneScoped;

    const Buffer* result = nullptr;

    do {
        mutex.lock();
        CZ_DEFER(mutex.unlock());

        // Already locked in writing mode.
        if (active_state == LOCKED_WRITING) {
            goto ret;
        }

        // Yield priority to a waiting thread.
        if (waiters_count > 0) {
            waiters_condition.signal_one();
            goto ret;
        }

        if (active_state == UNLOCKED) {
            // Lock in read mode.
            active_state = READER_0;
            // There are no waiters so we don't need to signal.
        } else {
            // Already locked in read mode.
            CZ_DEBUG_ASSERT(active_state >= READER_0);
            ++active_state;
        }

    } while (0);

    result = &buffer;

ret:
#ifdef TRACY_ENABLE
    context->AfterTryLockShared(result != nullptr);
#endif

    return result;
}

void Buffer_Handle::reduce_writing_to_reading() {
    ZoneScoped;

#ifdef TRACY_ENABLE
    context->AfterUnlock();
    const auto run_after = context->BeforeLockShared();
#endif

    {
        mutex.lock();
        CZ_DEFER(mutex.unlock());

        CZ_DEBUG_ASSERT(active_state == LOCKED_WRITING);

        active_state = READER_0;

        // Wake up all waiters so other readers will also run.
        waiters_condition.signal_all();
    }

#ifdef TRACY_ENABLE
    if (run_after) {
        context->AfterLockShared();
    }
#endif
}

Buffer* Buffer_Handle::increase_reading_to_writing() {
    ZoneScoped;

#ifdef TRACY_ENABLE
    context->AfterUnlockShared();
    const auto run_after = context->BeforeLock();
#endif

    do {
        mutex.lock();
        CZ_DEFER(mutex.unlock());

        CZ_DEBUG_ASSERT(active_state >= READER_0);

        // unlock()
        {
            // If we're the only reader then lock in writing mode.
            if (active_state == READER_0) {
                active_state = LOCKED_WRITING;
                break;
            }

            --active_state;
            CZ_DEBUG_ASSERT(active_state >= READER_0);
        }

        // lock_writing()
        {
            ++waiters_count;
            // Wait for exclusive access.
            while (active_state != UNLOCKED) {
                waiters_condition.wait(&mutex);
            }
            --waiters_count;

            active_state = LOCKED_WRITING;
        }
    } while (0);

#ifdef TRACY_ENABLE
    if (run_after) {
        context->AfterLock();
    }
#endif

    return &buffer;
}

void Buffer_Handle::unlock() {
    ZoneScoped;

    {
        mutex.lock();
        CZ_DEFER(mutex.unlock());

        CZ_DEBUG_ASSERT(active_state != UNLOCKED);

#ifdef TRACY_ENABLE
        if (active_state == LOCKED_WRITING) {
            context->AfterUnlock();
        } else {
            context->AfterUnlockShared();
        }
#endif

        // If we're either the only writer or reader then unlock.
        if (active_state <= READER_0) {
            active_state = UNLOCKED;
            waiters_condition.signal_one();
        } else {
            --active_state;
            CZ_DEBUG_ASSERT(active_state >= READER_0);
        }
    }
}

}
