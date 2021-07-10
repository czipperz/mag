#pragma once

#include <stdint.h>
#include <cz/arc.hpp>
#include <cz/buffer_array.hpp>
#include <cz/heap.hpp>
#include <cz/option.hpp>
#include <cz/vector.hpp>
#include "buffer_handle.hpp"
#include "job.hpp"
#include "key_map.hpp"
#include "key_remap.hpp"
#include "theme.hpp"

namespace mag {

struct Editor {
    cz::Vector<cz::Arc<Buffer_Handle> > buffers;

    Key_Remap key_remap;
    Key_Map key_map;
    cz::Heap_Vector<Command> misc_commands;
    Theme theme;

    cz::Buffer_Array copy_buffer;

    uint64_t buffer_counter;

    cz::Vector<Asynchronous_Job> pending_jobs;
    cz::Vector<Synchronous_Job> synchronous_jobs;

    void create();
    void drop();

    /// Schedule a job to be ran either asynchronously or synchronously.
    void add_asynchronous_job(Asynchronous_Job job);
    void add_synchronous_job(Synchronous_Job job);

    /// Remove the corresponding buffer.
    void kill(Buffer_Handle* buffer_handle);

    /// Do not decrement the reference count by calling `drop` on the return value.
    cz::Arc<Buffer_Handle> create_buffer(Buffer buffer);

    /// Do not decrement the reference count by calling `drop` on the return value.
    cz::Arc<Buffer_Handle> create_temp_buffer(cz::Str temp_name, cz::Option<cz::Str> dir = {});
};

}
