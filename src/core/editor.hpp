#pragma once

#include <stdint.h>
#include <cz/arc.hpp>
#include <cz/buffer_array.hpp>
#include <cz/heap.hpp>
#include <cz/option.hpp>
#include <cz/vector.hpp>
#include "core/buffer_handle.hpp"
#include "core/job.hpp"
#include "core/key_map.hpp"
#include "core/key_remap.hpp"
#include "core/theme.hpp"

namespace mag {

struct Editor {
    cz::Vector<cz::Arc<Buffer_Handle> > buffers;

    Key_Remap key_remap;
    Key_Map key_map;
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

    /// Takes ownership of the Arc, do not `drop`.
    void create_buffer(cz::Arc<Buffer_Handle> buffer_handle);
    /// Do not decrement the reference count by calling `drop` on the return value.
    cz::Arc<Buffer_Handle> create_buffer(Buffer buffer);
};

}
