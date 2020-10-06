#include "process.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <stdlib.h>
#include <Tracy.hpp>
#include <cz/defer.hpp>

namespace mag {

void File_Descriptor::close() {
#ifdef _WIN32
    if (handle != Null_) {
        CloseHandle(handle);
    }
#else
    if (fd != Null_) {
        ::close(fd);
    }
#endif
}

bool Input_File::open(const char* file) {
#ifdef _WIN32
    void* h = CreateFile(file, GENERIC_READ, FILE_SHARE_READ,
                         /* TODO: maybe the file should be inheritable? */ NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    handle = h;
    return true;
#else
    fd = ::open(file, O_RDONLY);
    return fd != -1;
#endif
}

bool File_Descriptor::set_non_blocking() {
#ifdef _WIN32
    DWORD mode = PIPE_NOWAIT;
    return SetNamedPipeHandleState(handle, &mode, NULL, NULL);
#else
    int res = fcntl(fd, F_GETFL);
    if (res < 0) {
        return false;
    }
    if (fcntl(fd, F_SETFL, res | O_NONBLOCK) < 0) {
        return false;
    }
    return true;
#endif
}

bool File_Descriptor::set_non_inheritable() {
#ifdef _WIN32
    return SetHandleInformation(handle, HANDLE_FLAG_INHERIT, FALSE);
#else
    int res = fcntl(fd, F_GETFD);
    if (res < 0) {
        return false;
    }
    if (fcntl(fd, F_SETFD, res | O_CLOEXEC) < 0) {
        return false;
    }
    return true;
#endif
}

int64_t Input_File::read(char* buffer, size_t size) {
#ifdef _WIN32
    DWORD bytes;
    if (ReadFile(handle, buffer, size, &bytes, NULL)) {
        return bytes;
    } else {
        return -1;
    }
#else
    return ::read(fd, buffer, size);
#endif
}

bool Output_File::open(const char* file) {
#ifdef _WIN32
    void* h = CreateFile(file, GENERIC_WRITE, 0,
                         /* TODO: maybe the file should be inheritable? */ NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    handle = h;
    return true;
#else
    fd = ::open(file, O_RDONLY);
    return fd != -1;
#endif
}

int64_t Output_File::write(const char* buffer, size_t size) {
#ifdef _WIN32
    DWORD bytes;
    if (WriteFile(handle, buffer, size, &bytes, NULL)) {
        return bytes;
    } else {
        return -1;
    }
#else
    return ::write(fd, buffer, size);
#endif
}

void read_to_string(Input_File file, cz::Allocator allocator, cz::String* out) {
    char buffer[1024];
    while (1) {
        ssize_t read_result = file.read(buffer, sizeof(buffer));
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

void Process_Options::close_all() {
    stdin.close();
    stdout.close();
#ifdef _WIN32
    if (stdout.handle != stderr.handle)
#else
    if (stdout.fd != stderr.fd)
#endif
    {
        stderr.close();
    }
}

bool create_pipe(Input_File* input, Output_File* output) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    return CreatePipe(&input->handle, &output->handle, &sa, 0);
#else
    int fds[2];
    if (pipe(fds) < 0) {
        return false;
    }

    input->fd = fds[0];
    output->fd = fds[1];
    return true;
#endif
}

bool create_process_input_pipe(Input_File* input, Output_File* output) {
    if (!create_pipe(input, output)) {
        return false;
    }

    if (!output->set_non_inheritable()) {
        input->close();
        output->close();
        return false;
    }

    return true;
}

bool create_process_output_pipe(Output_File* output, Input_File* input) {
    if (!create_pipe(input, output)) {
        return false;
    }

    if (!input->set_non_inheritable()) {
        input->close();
        output->close();
        return false;
    }

    return true;
}

bool create_process_pipes(Process_IO* io, Process_Options* options) {
    if (!create_process_input_pipe(&options->stdin, &io->stdin)) {
        return false;
    }

    if (!create_process_output_pipe(&options->stdout, &io->stdout)) {
        options->stdin.close();
        io->stdin.close();
        return false;
    }

    options->stderr = options->stdout;

    return true;
}

bool create_process_pipes(Process_IOE* io, Process_Options* options) {
    if (!create_process_input_pipe(&options->stdin, &io->stdin)) {
        return false;
    }

    if (!create_process_output_pipe(&options->stdout, &io->stdout)) {
        options->stdin.close();
        io->stdin.close();
        return false;
    }

    if (!create_process_output_pipe(&options->stderr, &io->stderr)) {
        options->stdout.close();
        io->stdout.close();
        options->stdin.close();
        io->stdin.close();
        return false;
    }

    return true;
}

void Process::kill() {
#ifdef _WIN32
    TerminateProcess(hProcess, -1);
#else
    ::kill(pid, SIGTERM);
#endif
}

int Process::join() {
#ifdef _WIN32
    WaitForSingleObject(hProcess, INFINITE);
    DWORD exitCode = -1;
    GetExitCodeProcess(hProcess, &exitCode);
    return exitCode;
#else
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else {
        return 127;
    }
#endif
}

#ifdef _WIN32
static bool launch_script_(char* script, Process_Options* options, HANDLE* hProcess) {
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdError = options->stderr;
    si.hStdOutput = options->stdout;
    si.hStdInput = options->stdin;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(nullptr, script, nullptr, nullptr, TRUE, 0, nullptr,
                        options->working_directory, &si, &pi)) {
        return false;
    }

    *hProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    return true;
}

static void escape_backslashes(cz::String* script, cz::Str arg, size_t i) {
    for (size_t j = i; j-- > 0;) {
        if (arg[j] == '\\') {
            script->reserve(cz::heap_allocator(), 1);
            script->push('\\');
        } else {
            break;
        }
    }
}

static void add_argument(cz::String* script, cz::Str arg) {
    script->reserve(cz::heap_allocator(), 3 + arg.len);
    script->push('"');

    for (size_t i = 0; i < arg.len; ++i) {
        if (arg[i] == '"') {
            escape_backslashes(script, arg, i);

            script->reserve(cz::heap_allocator(), 2);
            script->push('\\');
        }

        script->reserve(cz::heap_allocator(), 1);
        script->push(arg[i]);
    }

    escape_backslashes(script, arg, arg.len);

    script->reserve(cz::heap_allocator(), 2);
    script->push('"');
    script->push(' ');
}

bool Process::launch_script(const char* script, Process_Options* options) {
    cz::Str prefix = "cmd /C ";
    size_t len = strlen(script);

    char* copy = (char*)malloc(prefix.len + len + 1);
    if (!copy) {
        return false;
    }
    CZ_DEFER(free(copy));

    memcpy(copy, prefix.buffer, prefix.len);
    memcpy(copy + prefix.len, script, len);
    copy[prefix.len + len] = 0;

    return launch_script_(copy, options, &hProcess);
}

bool Process::launch_program(const char* const* args, Process_Options* options) {
    cz::String script = {};
    script.reserve(cz::heap_allocator(), 32);
    CZ_DEFER(script.drop(cz::heap_allocator()));

    for (const char* const* arg = args; *arg; ++arg) {
        add_argument(&script, *arg);
    }

    return launch_script_(script.buffer(), options, &hProcess);
}

#else

bool Process::launch_script(const char* script, Process_Options* options) {
    const char* args[] = {"/bin/sh", "-c", script, nullptr};
    return launch_program(args, options);
}

static void bind_pipe(int input, int output) {
    if (input != File_Descriptor::Null_) {
        dup2(input, output);
    } else {
        close(output);
    }
}

bool Process::launch_program(const char* const* args, Process_Options* options) {
    ZoneScoped;

    size_t num_args = 0;
    while (args[num_args]) {
        ++num_args;
    }

    char** new_args = (char**)malloc(sizeof(char*) * (num_args + 1));
    CZ_ASSERT(new_args);
    CZ_DEFER(free(new_args));
    for (size_t i = 0; i < num_args; ++i) {
        const char* arg = args[i];
        size_t len = strlen(arg);

        char* new_arg = (char*)malloc(len + 1);
        CZ_ASSERT(new_arg);
        memcpy(new_arg, arg, len);
        new_arg[len] = 0;

        new_args[i] = new_arg;
    }
    new_args[num_args] = nullptr;
    CZ_DEFER(for (size_t i = 0; i < num_args; ++i) { free(new_args[i]); });

    pid = fork();
    if (pid < 0) {
        return false;
    } else if (pid == 0) {  // child process
        bind_pipe(options->stdin.fd, 0);
        bind_pipe(options->stdout.fd, 1);
        bind_pipe(options->stderr.fd, 2);

        close(options->stdin.fd);
        close(options->stdout.fd);
        if (options->stderr.fd != options->stdout.fd) {
            close(options->stderr.fd);
        }

        if (options->working_directory) {
            chdir(options->working_directory);
        }

        // Launch the script by running it through the shell.
        execv(new_args[0], new_args);

        // If exec returns there is an error launching.
        const char* message = "Error executing /bin/sh";
        write(2, message, strlen(message));
        exit(errno);
    } else {  // parent process
        return true;
    }
}
#endif

}
