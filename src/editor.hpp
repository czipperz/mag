#pragma once

#include <stdint.h>
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
    cz::Vector<Buffer_Handle*> buffers;

    Key_Map key_map;
    Theme theme;

    cz::Buffer_Array copy_buffer;

    uint64_t buffer_counter;
    uint64_t temp_counter;

    cz::Vector<Job> jobs;

    void create() { copy_buffer.create(); }

    void drop() {
        for (size_t i = 0; i < buffers.len(); ++i) {
            buffers[i]->drop();
            cz::heap_allocator().dealloc({buffers[i], sizeof(Buffer_Handle)});
        }
        buffers.drop(cz::heap_allocator());

        key_map.drop();
        theme.drop(cz::heap_allocator());
        copy_buffer.drop();

        for (size_t i = 0; i < jobs.len(); ++i) {
            jobs[i].kill(this, jobs[i].data);
        }
        jobs.drop(cz::heap_allocator());
    }

    void add_job(Job job) {
        jobs.reserve(cz::heap_allocator(), 1);
        jobs.push(job);
    }

    void tick_jobs() {
        for (size_t i = 0; i < jobs.len(); ++i) {
            if (jobs[i].tick(this, jobs[i].data)) {
                // Todo: optimize by doing swap with last
                jobs.remove(i);
                --i;
            }
        }
    }

    Buffer_Handle* lookup(Buffer_Id id) {
        size_t index;
        if (binary_search_buffer_id(id, &index)) {
            return buffers[index];
        }
        return nullptr;
    }

    void kill(Buffer_Id id) {
        size_t index;
        if (binary_search_buffer_id(id, &index)) {
            buffers[index]->drop();
            cz::heap_allocator().dealloc({buffers[index], sizeof(Buffer_Handle)});
            buffers.remove(index);
        }
    }

    Buffer_Id create_buffer(Buffer buffer) {
        Buffer_Handle* buffer_handle = cz::heap_allocator().create<Buffer_Handle>();
        buffer_handle->init({buffer_counter++}, buffer);
        buffers.reserve(cz::heap_allocator(), 1);
        buffers.push(buffer_handle);
        return buffer_handle->id;
    }

    Buffer_Id create_temp_buffer(cz::Str temp_name, cz::Option<cz::Str> dir = {});

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
