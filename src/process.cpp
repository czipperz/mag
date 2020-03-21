#include "process.hpp"

#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace mag {

bool run_script_synchronously(const char* script,
                              const char* working_directory,
                              cz::Allocator allocator,
                              cz::String* out,
                              int* return_value) {
    const char* shell = "/bin/sh";
    const char* args[] = {shell, "-c", script, nullptr};
    return run_process_synchronously(shell, args, working_directory, allocator, out, return_value);
}

bool run_process_synchronously(const char* path,
                               const char** args,
                               const char* working_directory,
                               cz::Allocator allocator,
                               cz::String* out,
                               int* return_value) {
    int pipe_fds[2];  // 0 = read, 1 = write
    if (pipe(pipe_fds) < 0) {
        return false;
    }

    pid_t fork_result = fork();
    if (fork_result < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    } else if (fork_result == 0) {  // child process
        // Make stdout (1) and stderr (2) write to the pipe then decrement the reference count.
        close(pipe_fds[0]);
        close(0);
        dup2(pipe_fds[1], 1);
        dup2(pipe_fds[1], 2);
        close(pipe_fds[1]);

        if (working_directory) {
            chdir(working_directory);
        }

        // Launch the script by running it through the shell.
        execv(path, (char**)args);

        // If exec returns there is an error launching.
        const char* message = "Error executing /bin/sh";
        write(1, message, strlen(message));
        exit(errno);
    } else {  // parent process
        close(pipe_fds[1]);
        char buffer[1024];
        while (1) {
            ssize_t read_result = read(pipe_fds[0], buffer, sizeof(buffer));
            if (read_result < 0) {
                // Todo: what do we do here?  I'm just ignoring the error for now
            } else if (read_result == 0) {
                // End of file
                break;
            } else {
                out->reserve(allocator, read_result);
                out->append({buffer, (size_t)read_result});
            }
        }
        close(pipe_fds[0]);
        int status;
        wait(&status);
        if (WIFEXITED(status)) {
            *return_value = WEXITSTATUS(status);
        } else {
            *return_value = 127;
        }
        return true;
    }
}

}
