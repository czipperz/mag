#include "editor.hpp"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <cz/defer.hpp>
#include <cz/option.hpp>

namespace mag {

Buffer_Id Editor::create_temp_buffer(cz::Str temp_name, cz::Option<cz::Str> dir) {
    Buffer_Handle* buffer_handle = cz::heap_allocator().create<Buffer_Handle>();

    uint64_t number = ++temp_counter;
    uint64_t digits = (uint64_t)log10(number) + 1;

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

    buffer.name.reserve(cz::heap_allocator(), 3 + temp_name.len + digits);
    buffer.name.push('*');
    buffer.name.append(temp_name);
    buffer.name.push(' ');
    sprintf(buffer.name.end(), "%" PRIu64, number);
    buffer.name.set_len(buffer.name.len() + digits);
    buffer.name.push('*');

    buffer_handle->init({buffer_counter++}, buffer);
    buffers.reserve(cz::heap_allocator(), 1);
    buffers.push(buffer_handle);
    return buffer_handle->id;
}

}
