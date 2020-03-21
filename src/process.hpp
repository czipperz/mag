#pragma once

#include <cz/allocator.hpp>
#include <cz/string.hpp>

namespace mag {

bool run_process_synchronously(const char* path,
                               const char** args,
                               const char* working_directory,
                               cz::Allocator,
                               cz::String* out,
                               int* return_value);

bool run_script_synchronously(const char* script,
                              const char* working_directory,
                              cz::Allocator allocator,
                              cz::String* out,
                              int* return_value);

}
