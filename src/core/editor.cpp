#include "editor.hpp"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <cz/defer.hpp>
#include <cz/option.hpp>
#include "core/command_macros.hpp"
#include "custom/config.hpp"

namespace mag {

void Editor::create() {
    copy_buffer.init();
}

void Editor::drop() {
    for (size_t i = 0; i < buffers.len; ++i) {
        buffers[i].drop();
    }
    buffers.drop(cz::heap_allocator());

    key_map.drop();
    key_remap.drop();
    theme.drop();
    copy_buffer.drop();

    for (size_t i = 0; i < pending_jobs.len; ++i) {
        pending_jobs[i].kill(pending_jobs[i].data);
    }
    pending_jobs.drop(cz::heap_allocator());
    for (size_t i = 0; i < synchronous_jobs.len; ++i) {
        synchronous_jobs[i].kill(synchronous_jobs[i].data);
    }
    synchronous_jobs.drop(cz::heap_allocator());
}

void Editor::add_asynchronous_job(Asynchronous_Job job) {
    pending_jobs.reserve(cz::heap_allocator(), 1);
    pending_jobs.push(job);
}

void Editor::add_synchronous_job(Synchronous_Job job) {
    synchronous_jobs.reserve(cz::heap_allocator(), 1);
    synchronous_jobs.push(job);
}

void Editor::kill(Buffer_Handle* buffer) {
    for (size_t i = buffers.len; i-- > 0;) {
        if (buffers[i].get() == buffer) {
            buffers[i].drop();
            buffers.remove(i);
            break;
        }
    }
}

void Editor::create_buffer(cz::Arc<Buffer_Handle> buffer_handle) {
    ZoneScoped;

    {
        WITH_BUFFER_HANDLE(buffer_handle);
        buffer->id = {buffer_counter++};
        custom::buffer_created_callback(this, buffer, buffer_handle);
    }

    buffers.reserve(cz::heap_allocator(), 1);
    buffers.push(buffer_handle);
}

cz::Arc<Buffer_Handle> Editor::create_buffer(Buffer buffer) {
    cz::Arc<Buffer_Handle> buffer_handle = create_buffer_handle(buffer);
    create_buffer(buffer_handle);
    return buffer_handle;
}

}
