#pragma once

#include <cz/allocator.hpp>
#include <cz/string.hpp>

namespace mag {

bool run_process_synchronously(const char* script,
                               cz::Allocator allocator,
                               cz::String* out,
                               int* return_value);

}
