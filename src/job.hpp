#pragma once

#include <cz/str.hpp>

namespace mag {
struct Buffer_Id;
struct Client;
struct Editor;
struct Process;

/// A Job represents a task to be performed in the background.
struct Job {
    /// Run one tick of the job.  Returns `true` iff the job has halted.
    ///
    /// When the job has halted, `tick` will no longer be invoked.
    bool (*tick)(Editor* editor, void* data);

    /// Cleanup the `Job` because it is forcibly no longer going to be ran.
    ///
    /// This is invoked when the editor closed.  This is not invoked when the tasks halts (`tick`
    /// returns true), because often times you want different cleanup behavior on a successful exit
    /// than an unsuccessful exit.
    void (*kill)(Editor* editor, void* data);

    void* data;
};

Job job_process_append(Buffer_Id buffer_id, Process process);
bool run_console_command(Client* client,
                         Editor* editor,
                         const char* working_directory,
                         const char* script,
                         cz::Str buffer_name,
                         cz::Str error);

}
