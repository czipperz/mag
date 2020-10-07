#pragma once

#include <stdint.h>
#include <cz/allocator.hpp>
#include <cz/string.hpp>

#ifdef _WIN32
#else
#include <sys/types.h>
#endif

namespace mag {

struct File_Descriptor {
#ifdef _WIN32
    void* handle = Null_;

    static constexpr void* const Null_ = (void*)-1;
#else
    int fd = Null_;

    static constexpr const int Null_ = -1;
#endif

    void close();

    /// Set the file descriptor to return swiftly from IO operations.
    ///
    /// This is most useful when used on an `Input_File`.  In this case read will immediately
    /// return when nothing can be read rather than stalling.
    bool set_non_blocking();

    /// Set the file descriptor to be automatically closed when a process is created.
    bool set_non_inheritable();
};

struct Input_File : File_Descriptor {
    bool open(const char* file);

    /// Read up to `size` bytes from the file into `buffer`.
    ///
    /// Returns the number of bytes actually read, or `-1` on failure.
    int64_t read(char* buffer, size_t size);
};

struct Output_File : File_Descriptor {
    bool open(const char* file);

    /// Write `size` bytes from `buffer` to the file.
    ///
    /// Returns the number of bytes actually written, or `-1` on failure.
    int64_t write(const char* buffer, size_t size);
};

struct Contents;
bool create_temp_file(Input_File* fd, const Contents& contents);
bool create_temp_file(Input_File* fd, const char* contents);

void read_to_string(Input_File, cz::Allocator allocator, cz::String* string);

struct Process_Options {
    Input_File std_in;
    Output_File std_out;
    Output_File std_err;

    /// The directory to run the process from.
    const char* working_directory = nullptr;

    void close_all();
};

/// Create a pipe where both ends are inheritable.
bool create_pipe(Input_File*, Output_File*);

/// Create a pipe where the writing side is non-inheritable.  Use this for `Process_Options::stdin`.
bool create_process_input_pipe(Input_File*, Output_File*);
/// Create a pipe where the reading side is non-inheritable.  Use this for `Process_Options::stdout`
/// or `Process_Options::stderr`.
bool create_process_output_pipe(Output_File*, Input_File*);

struct Process_IOE {
    Output_File std_in;
    Input_File std_out;
    Input_File std_err;
};

struct Process_IO {
    Output_File std_in;
    Input_File std_out;
};

bool create_process_pipes(Process_IO*, Process_Options*);
bool create_process_pipes(Process_IOE*, Process_Options*);

class Process {
#ifdef _WIN32
    void* hProcess;
#else
    pid_t pid;
#endif

public:
    /// Launch a program.  The first argument must be the same the path to invoke.
    ///
    /// The processes `stdin`, `stdout`, and `stderr` streams are bound to the `options`' streams.
    /// The streams in `options` are not closed by this function.  Any files that are null (the
    /// default) are closed instead of being bound (in the new process).
    ///
    /// The return value is `true` if the program was successfully launched.
    bool launch_program(const char* const* args, Process_Options* options);

    /// Launch a script as if it was ran on the command line.
    ///
    /// This runs the script through `cmd` on Windows and `/bin/sh` otherwise.
    ///
    /// See also `launch_program` for information on how `options` are handled.
    bool launch_script(const char* script, Process_Options* options);

    void kill();
    int join();
};

}
