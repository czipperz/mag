#pragma once

#include <stdint.h>
#include <cz/allocator.hpp>
#include <cz/string.hpp>

#ifdef _WIN32
#else
#include <sys/types.h>
#endif

namespace mag {

class Process {
#ifdef _WIN32
#else
    int fd;
    pid_t pid;
#endif

public:
    bool launch_program(const char* path, const char** args, const char* working_directory);
    bool launch_script(const char* script, const char* working_directory);

    void kill();
    int join();

    bool set_read_blocking();
    int64_t read(char* buffer, size_t buffer_size);
    void read_to_string(cz::Allocator allocator, cz::String* out);

    void destroy();
};

}
