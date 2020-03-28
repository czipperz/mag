// Expose pipe2(2).  See feature_test_macros(7).
#define _GNU_SOURCE
#include "process.hpp"

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <Tracy.hpp>

namespace mag {

int64_t Process::read(char* buffer, size_t buffer_size) {
    return ::read(fd, buffer, buffer_size);
}

void Process::destroy() {
    close(fd);
}

bool Process::set_read_blocking() {
    int res = fcntl(fd, F_GETFL);
    if (res < 0) {
        return false;
    }
    if (fcntl(fd, F_SETFL, res & ~O_NONBLOCK) < 0) {
        return false;
    }
    return true;
}

void Process::read_to_string(cz::Allocator allocator, cz::String* out) {
    set_read_blocking();

    char buffer[1024];
    while (1) {
        ssize_t read_result = read(buffer, sizeof(buffer));
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
}

void Process::kill() {
    ::kill(pid, SIGTERM);
}

int Process::join() {
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else {
        return 127;
    }
}

bool Process::launch_script(const char* script, const char* working_directory) {
    const char* shell = "/bin/sh";
    const char* args[] = {shell, "-c", script, nullptr};
    return launch_program(shell, args, working_directory);
}

bool Process::launch_program(const char* path, const char** args, const char* working_directory) {
    ZoneScoped;

    int pipe_fds[2];  // 0 = read, 1 = write
    if (pipe2(pipe_fds, O_NONBLOCK) < 0) {
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    } else if (pid == 0) {  // child process
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
        this->fd = pipe_fds[0];
        this->pid = pid;
        return true;
    }
}

}
