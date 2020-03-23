#include "editor.hpp"

#include <math.h>
#include <stdio.h>
#include <cz/defer.hpp>
#include <inttypes.h>

namespace mag {

Buffer_Id Editor::create_temp_buffer(cz::Str temp_name) {
    Buffer_Handle* buffer_handle = cz::heap_allocator().create<Buffer_Handle>();

    uint64_t number = ++temp_counter;
    uint64_t digits = (uint64_t)log10(number) + 1;
    cz::String path = {};
    path.reserve(cz::heap_allocator(), 2 + temp_name.len + 1 + digits);
    CZ_DEFER(path.drop(cz::heap_allocator()));
    path.push('*');
    path.append(temp_name);
    path.push(' ');
    sprintf(path.end(), "%" PRIu64, number);
    path.set_len(path.len() + digits);
    path.push('*');

    buffer_handle->init({buffer_counter++}, path);
    buffers.reserve(cz::heap_allocator(), 1);
    buffers.push(buffer_handle);
    return buffer_handle->id;
}

}
