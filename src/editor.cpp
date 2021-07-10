#include "editor.hpp"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <cz/defer.hpp>
#include <cz/option.hpp>
#include "custom/config.hpp"

namespace mag {

void Editor::create() {
    copy_buffer.init();
}

void Editor::drop() {
    for (size_t i = 0; i < buffers.len(); ++i) {
        buffers[i].drop();
    }
    buffers.drop(cz::heap_allocator());

    misc_commands.drop();
    key_map.drop();
    key_remap.drop();
    theme.drop();
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

void Editor::add_asynchronous_job(Asynchronous_Job job) {
    pending_jobs.reserve(cz::heap_allocator(), 1);
    pending_jobs.push(job);
}

void Editor::add_synchronous_job(Synchronous_Job job) {
    synchronous_jobs.reserve(cz::heap_allocator(), 1);
    synchronous_jobs.push(job);
}

void Editor::kill(Buffer_Handle* buffer) {
    for (size_t i = buffers.len(); i-- > 0;) {
        if (buffers[i].get() == buffer) {
            buffers[i].drop();
            buffers.remove(i);
            break;
        }
    }
}

static bool binary_search_buffers(cz::Slice<cz::Arc<Buffer_Handle> > buffers,
                                  cz::Arc<Buffer_Handle> buffer_handle,
                                  size_t* index) {
    size_t start = 0;
    size_t end = buffers.len;
    while (start < end) {
        size_t mid = (start + end) / 2;
        Buffer_Handle* test = buffers[mid].get();
        if (test == buffer_handle.get()) {
            *index = mid;
            return true;
        } else if (test < buffer_handle.get()) {
            start = mid + 1;
        } else {
            end = mid;
        }
    }

    *index = start;
    return false;
}

cz::Arc<Buffer_Handle> Editor::create_buffer(Buffer buffer) {
    ZoneScoped;

    custom::buffer_created_callback(this, &buffer);

    cz::Arc<Buffer_Handle> buffer_handle;
    buffer_handle.init_emplace();
    buffer.id = {buffer_counter++};
    buffer_handle->init(buffer);

    buffers.reserve(cz::heap_allocator(), 1);
    buffers.push(buffer_handle);

    return buffer_handle;
}

cz::Arc<Buffer_Handle> Editor::create_temp_buffer(cz::Str temp_name, cz::Option<cz::Str> dir) {
    ZoneScoped;

    Buffer buffer = {};

    if (dir.is_present) {
        bool has_forward_slash = dir.value.ends_with("/");
        buffer.directory.reserve(cz::heap_allocator(), dir.value.len + !has_forward_slash + 1);
        buffer.directory.append(dir.value);
        if (!has_forward_slash) {
            buffer.directory.push('/');
        }
        buffer.directory.null_terminate();
    }

    buffer.type = Buffer::TEMPORARY;
    buffer.read_only = true;

    buffer.name.reserve(cz::heap_allocator(), 2 + temp_name.len);
    buffer.name.push('*');
    buffer.name.append(temp_name);
    buffer.name.push('*');

    return create_buffer(buffer);
}

}
