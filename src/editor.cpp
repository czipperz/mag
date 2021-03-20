#include "editor.hpp"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <cz/defer.hpp>
#include <cz/option.hpp>

namespace mag {

Buffer_Id Editor::create_temp_buffer(cz::Str temp_name, cz::Option<cz::Str> dir) {
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
