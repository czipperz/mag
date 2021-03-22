#pragma once

#include <stdint.h>
#include <cz/arc.hpp>
#include <cz/heap.hpp>
#include <cz/option.hpp>
#include <cz/vector.hpp>
#include "buffer_handle.hpp"
#include "buffer_id.hpp"
#include "job.hpp"
#include "key_map.hpp"
#include "theme.hpp"

namespace mag {

struct Editor {
    cz::Vector<cz::Arc<Buffer_Handle> > buffers;

    Key_Map key_map;
    Theme theme;

    cz::Buffer_Array copy_buffer;

    uint64_t buffer_counter;

    cz::Vector<Asynchronous_Job> pending_jobs;
    cz::Vector<Synchronous_Job> synchronous_jobs;

    void create() { copy_buffer.create(); }

    void drop() {
        for (size_t i = 0; i < buffers.len(); ++i) {
            buffers[i].drop();
        }
        buffers.drop(cz::heap_allocator());

        key_map.drop();
        theme.drop(cz::heap_allocator());
        copy_buffer.drop();

        for (size_t i = 0; i < pending_jobs.len(); ++i) {
            pending_jobs[i].kill(pending_jobs[i].data);
        }
        pending_jobs.drop(cz::heap_allocator());
        for (size_t i = 0; i < synchronous_jobs.len(); ++i) {
            synchronous_jobs[i].kill(synchronous_jobs[i].data);
        }
        synchronous_jobs.drop(cz::heap_allocator());
    }

    void add_asynchronous_job(Asynchronous_Job job) {
        pending_jobs.reserve(cz::heap_allocator(), 1);
        pending_jobs.push(job);
    }

    void add_synchronous_job(Synchronous_Job job) {
        synchronous_jobs.reserve(cz::heap_allocator(), 1);
        synchronous_jobs.push(job);
    }

    /// Try to look up a buffer by its id.  Doesn't increment the reference count.
    /// If the buffer doesn't exist then returns `false`.
    bool try_lookup(Buffer_Id id, cz::Arc<Buffer_Handle>* buffer_handle) {
        size_t index;
        if (binary_search_buffer_id(id, &index)) {
            *buffer_handle = buffers[index];
            return true;
        }
        return false;
    }

    /// Look up a buffer by its id.  Doesn't increment the reference count.
    /// Panics if the buffer doesn't exist.
    cz::Arc<Buffer_Handle> lookup(Buffer_Id id) {
        cz::Arc<Buffer_Handle> buffer_handle;
        if (!try_lookup(id, &buffer_handle)) {
            CZ_PANIC("mag::Editor::lookup on invalid buffer id.");
        }
        return buffer_handle;
    }

    void kill(Buffer_Id id) {
        size_t index;
        if (binary_search_buffer_id(id, &index)) {
            buffers[index].drop();
            buffers.remove(index);
        }
    }

    /// Do not decrement the reference count by calling `drop` on the return value.
    cz::Arc<Buffer_Handle> create_buffer(Buffer buffer) {
        cz::Arc<Buffer_Handle> buffer_handle;
        buffer_handle.init_emplace();
        buffer_handle->init({buffer_counter++}, buffer);

        buffers.reserve(cz::heap_allocator(), 1);
        buffers.push(buffer_handle);

        return buffer_handle;
    }

    /// Do not decrement the reference count by calling `drop` on the return value.
    cz::Arc<Buffer_Handle> create_temp_buffer(cz::Str temp_name, cz::Option<cz::Str> dir = {});

private:
    bool binary_search_buffer_id(Buffer_Id id, size_t* index) {
        size_t start = 0;
        size_t end = buffers.len();
        while (start < end) {
            size_t mid = (start + end) / 2;
            if (buffers[mid]->id.value == id.value) {
                *index = mid;
                return true;
            } else if (buffers[mid]->id.value < id.value) {
                start = mid + 1;
            } else {
                end = mid;
            }
        }

        return false;
    }
};

}
