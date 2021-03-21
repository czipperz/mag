#pragma once

#include <cz/arc.hpp>
#include <cz/str.hpp>

namespace cz {
struct Process;
struct Input_File;
}

namespace mag {
struct Buffer_Id;
struct Buffer_Handle;
struct Client;
struct Editor;

/// A Job represents a task to be performed in the background.
struct Job {
    /// Run one tick of the job.  Returns `true` iff the job has halted.
    ///
    /// When the job has halted, `tick` will no longer be invoked.
    bool (*tick)(void* data);

    /// Cleanup the `Job` because it is forcibly no longer going to be ran.
    ///
    /// This is invoked when the editor closed.  This is not invoked when the tasks halts (`tick`
    /// returns true), because often times you want different cleanup behavior on a successful exit
    /// than an unsuccessful exit.
    void (*kill)(void* data);

    void* data;
};

Job job_process_append(cz::Arc_Weak<Buffer_Handle> buffer_handle,
                       cz::Process process,
                       cz::Input_File output);
bool run_console_command(Client* client,
                         Editor* editor,
                         const char* working_directory,
                         cz::Str script,
                         cz::Str buffer_name,
                         cz::Str error);
bool run_console_command(Client* client,
                         Editor* editor,
                         const char* working_directory,
                         cz::Slice<cz::Str> args,
                         cz::Str buffer_name,
                         cz::Str error);

bool run_console_command_in(Client* client,
                            Editor* editor,
                            Buffer_Id buffer_id,
                            const char* working_directory,
                            cz::Str script,
                            cz::Str error);

}
