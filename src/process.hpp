#pragma once

#include <cz/allocator.hpp>
#include <cz/string.hpp>

#include <sys/types.h>

namespace mag {

class Process {
    int fd;
    pid_t pid;

public:
    bool launch_program(const char* path, const char** args, const char* working_directory);
    bool launch_script(const char* script, const char* working_directory);

    int join();

    bool set_read_blocking();
    int64_t read(char* buffer, size_t buffer_size);
    void read_to_string(cz::Allocator allocator, cz::String* out);

    void destroy();
};

}
