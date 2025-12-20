#pragma once

#include <stdint.h>
#include <cz/allocator.hpp>
#include <cz/slice.hpp>
#include <cz/str.hpp>
#include "core/contents.hpp"

namespace mag {
namespace prose {

struct File_Messages {
    cz::Slice<uint64_t> lines;
    cz::Slice<uint64_t> columns;
    cz::Slice<cz::Str> messages;
};

struct All_Messages {
    cz::Slice<cz::Str> file_names;
    cz::Slice<File_Messages> file_messages;
};

All_Messages parse_errors(Contents_Iterator it, cz::Allocator buffer_array_allocator);

}
}
