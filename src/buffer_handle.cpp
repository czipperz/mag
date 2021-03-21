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

    id = buffer_id;

    this->buffer = buffer;
    this->buffer.init();
}

void Buffer_Handle::drop() {
    buffer.drop();

    mutex.drop();
    waiters_condition.drop();
}

Buffer* Buffer_Handle::lock_writing() {
    ZoneScoped;

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

    return &buffer;
}

const Buffer* Buffer_Handle::lock_reading() {
    ZoneScoped;

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

    return &buffer;
}

const Buffer* Buffer_Handle::try_lock_reading() {
    ZoneScoped;

    {
        mutex.lock();
        CZ_DEFER(mutex.unlock());

        // Already locked in writing mode.
        if (active_state == LOCKED_WRITING) {
            return nullptr;
        }

        // Yield priority to a waiting thread.
        if (waiters_count > 0) {
            waiters_condition.signal_one();
            return nullptr;
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
    }

    return &buffer;
}

void Buffer_Handle::reduce_writing_to_reading() {
    ZoneScoped;

    {
        mutex.lock();
        CZ_DEFER(mutex.unlock());

        CZ_DEBUG_ASSERT(active_state == LOCKED_WRITING);

        active_state = READER_0;

        // Wake up all waiters so other readers will also run.
        waiters_condition.signal_all();
    }
}

Buffer* Buffer_Handle::increase_reading_to_writing() {
    ZoneScoped;

    {
        mutex.lock();
        CZ_DEFER(mutex.unlock());

        CZ_DEBUG_ASSERT(active_state >= READER_0);

        // unlock()
        {
            // If we're the only reader then lock in writing mode.
            if (active_state == READER_0) {
                active_state = LOCKED_WRITING;
                return &buffer;
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
    }

    return &buffer;
}

void Buffer_Handle::unlock() {
    ZoneScoped;

    {
        mutex.lock();
        CZ_DEFER(mutex.unlock());

        CZ_DEBUG_ASSERT(active_state != UNLOCKED);

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
