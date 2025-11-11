#include "buffer_handle.hpp"

#include <cz/assert.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/heap_vector.hpp>
#include <cz/string.hpp>
#include <tracy/Tracy.hpp>

namespace mag {

enum : uint32_t {
    UNLOCKED = 0,
    LOCKED_WRITING = 1,
    READER_0 = 2,
};

void Buffer_Handle::init(Buffer buffer) {
    mutex.init();
    waiters_condition.init();

    waiters_count = 0;
    active_state = UNLOCKED;

#ifdef TRACY_ENABLE
    context = new tracy::SharedLockableCtx([]() -> const tracy::SourceLocationData* {
        static constexpr tracy::SourceLocationData srcloc{nullptr, "mag::Buffer_Handle", __FILE__,
                                                          __LINE__, 0};
        return &srcloc;
    }());
    context->CustomName(buffer.name.buffer, buffer.name.len);
#endif

#ifndef NDEBUG
    associated_threads = {};
    associated_threads.reserve(cz::heap_allocator(), 1);
#endif

    this->buffer = buffer;
    this->buffer.init();
}

void Buffer_Handle::drop() {
    buffer.drop();

#ifndef NDEBUG
    associated_threads.drop(cz::heap_allocator());
#endif

    mutex.drop();
    waiters_condition.drop();

#ifdef TRACY_ENABLE
    delete context;
#endif
}

#ifndef NDEBUG
static bool already_locked(cz::Slice<uint64_t> associated_threads, size_t* index) {
    uint64_t value = tracy::GetThreadHandle();
    for (size_t i = 0; i < associated_threads.len; ++i) {
        if (associated_threads[i] == value) {
            *index = i;
            return true;
        }
    }
    return false;
}
#endif

#ifndef NDEBUG
static thread_local cz::Heap_Vector<cz::String> acquired_buffers = {};

static void push_acquired_buffer(Buffer* buffer) {
    acquired_buffers.reserve(1);
    cz::String name = {};
    buffer->render_name(cz::heap_allocator(), &name);
    name.realloc_null_terminate(cz::heap_allocator());
    acquired_buffers.push(name);
}
#endif

Buffer* Buffer_Handle::lock_writing() {
    ZoneScoped;

#ifdef TRACY_ENABLE
    const auto run_after = context->BeforeLock();
#endif

    {
        mutex.lock();
        CZ_DEFER(mutex.unlock());

#ifndef NDEBUG
        size_t index;
        if (already_locked(associated_threads, &index)) {
            CZ_PANIC(
                "Buffer_Handle::lock_writing: This thread has already "
                "locked the Buffer_Handle so we are deadlocking");
        }

        if (acquired_buffers.len != 0) {
            CZ_PANIC(
                "To prevent any potential deadlocks, each thread can only acquire a write lock on "
                "a Buffer if it is not locking any other Buffers");
        }
#endif

        ++waiters_count;
        // Wait for exclusive access.
        while (active_state != UNLOCKED) {
            waiters_condition.wait(&mutex);
        }
        --waiters_count;

        active_state = LOCKED_WRITING;

#ifndef NDEBUG
        push_acquired_buffer(&buffer);

        associated_threads.reserve(cz::heap_allocator(), 1);
        associated_threads.push(tracy::GetThreadHandle());
#endif
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

#ifndef NDEBUG
        if (active_state == LOCKED_WRITING) {
            size_t index;
            if (already_locked(associated_threads, &index)) {
                CZ_PANIC(
                    "Buffer_Handle::lock_reading: This thread has already "
                    "locked the Buffer_Handle so we are deadlocking");
            }
        }
#endif

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

#ifndef NDEBUG
        push_acquired_buffer(&buffer);

        associated_threads.reserve(cz::heap_allocator(), 1);
        associated_threads.push(tracy::GetThreadHandle());
#endif
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

    {
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

#ifndef NDEBUG
        push_acquired_buffer(&buffer);

        associated_threads.reserve(cz::heap_allocator(), 1);
        associated_threads.push(tracy::GetThreadHandle());
#endif
    }

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

#ifndef NDEBUG
        size_t index;
        if (already_locked(associated_threads, &index)) {
            associated_threads.remove(index);
        }

        CZ_ASSERT(acquired_buffers.len >= 1);
        acquired_buffers.pop().drop(cz::heap_allocator());
#endif

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

cz::Arc<Buffer_Handle> create_buffer_handle(Buffer buffer) {
    cz::Arc<Buffer_Handle> buffer_handle;
    buffer_handle.init_emplace();
    buffer_handle->init(buffer);
    return buffer_handle;
}

}
