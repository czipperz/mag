#include "editor.hpp"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <cz/defer.hpp>
#include <cz/option.hpp>
#include "custom/config.hpp"

namespace mag {

cz::Arc<Buffer_Handle> Editor::create_buffer(Buffer buffer) {
    ZoneScoped;

    custom::buffer_created_callback(this, &buffer);

    cz::Arc<Buffer_Handle> buffer_handle;
    buffer_handle.init_emplace();
    buffer_handle->init({buffer_counter++}, buffer);

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
