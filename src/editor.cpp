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

    cz::String path = {};
    bool has_forward_slash = dir.is_present && dir.value.ends_with("/");
    path.reserve(cz::heap_allocator(),
                 (dir.is_present ? dir.value.len + !has_forward_slash + 2 : 0) + 1 + temp_name.len +
                     1 + digits + 1);
    CZ_DEFER(path.drop(cz::heap_allocator()));

    if (dir.is_present) {
        path.append(dir.value);
        if (!has_forward_slash) {
            path.push('/');
        }
        path.append(": ");
    }

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
