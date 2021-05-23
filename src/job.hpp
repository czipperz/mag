#pragma once

#include <cz/arc.hpp>
#include <cz/str.hpp>
#include <cz/vector.hpp>

namespace cz {
struct Process;
struct Input_File;
}

namespace mag {
struct Buffer_Id;
struct Buffer_Handle;
struct Client;
struct Editor;
struct Run_Jobs;

struct Synchronous_Job;

/// Allows for `Asynchronous_Job`s to interact with the editor's state in controlled ways.
struct Asynchronous_Job_Handler {
private:
    cz::Vector<Synchronous_Job> pending_jobs;
    friend struct Run_Jobs;

public:
    /// Spawn a synchronous job.  A synchronous job is allowed to use the
    /// `Editor` and `Client` making it much more powerful.  However, because
    /// it runs in the main thread, it is more expensive.
    void add_synchronous_job(Synchronous_Job);

    /// Show a message to the user.
    void show_message(cz::Str message);
};

namespace Job_Tick_Result_ {
/// The result of a `tick` call on a job.
enum Job_Tick_Result {
    /// The job has finished.  It will be removed from the job list.
    FINISHED,
    /// The job has made some progress.  It will be re-scheduled to do more work.
    MADE_PROGRESS,
    /// The job has not made progress.  If all jobs are stalled the job thread will sleep.
    STALLED,
};
}
using Job_Tick_Result_::Job_Tick_Result;

/// An `Asynchronous_Job` represents a task to be performed in the background.
///
/// It is thread safe to use a `Buffer` by storing it as a `cz::Arc_Weak<Buffer_Handle>` (don't
/// store it as a `cz::Arc` because the user should be able to kill the buffer at any time).
/// Obviously prefer to lock the buffer in reading mode for as long as possible to prevent stalls
/// in the main thread.  Use `Buffer_Handle::increase_reading_to_writing` to commit the results.
///
/// It is not thread safe to use the `Editor`, `Client`, or any `Window`s.
///
/// To spawn an `Asynchronous_Job` use `Editor::add_asynchronous_job`.
/// See `job_process_append` for example code.
struct Asynchronous_Job {
    /// Run one tick of the job.
    Job_Tick_Result (*tick)(Asynchronous_Job_Handler*, void* data);

    /// Cleanup the job because it is forcibly no longer going to be ran.
    ///
    /// This is invoked when the editor closed.  This is not invoked when the task
    /// finishes (`tick` returns `Job_Tick_Result::FINISHED`), because often times you want
    /// different cleanup behavior on a successful exit than an unsuccessful exit.
    void (*kill)(void* data);

    void* data;
};

/// A `Synchronous_Job` represents a task to be performed on the main thread inbetween frames.
///
/// A synchronous job can be spawned either by `Editor::add_synchronous_job` or by
/// an asynchronous job calling `Asynchronous_Job_Handler::add_synchronous_job`.
struct Synchronous_Job {
    /// Run one tick of the job.
    Job_Tick_Result (*tick)(Editor* editor, Client* client, void* data);

    /// Cleanup the job because it is forcibly no longer going to be ran.
    ///
    /// This is invoked when the editor closed.  This is not invoked when the task
    /// finishes (`tick` returns `Job_Tick_Result::FINISHED`), because often times you want
    /// different cleanup behavior on a successful exit than an unsuccessful exit.
    void (*kill)(void* data);

    void* data;
};

Asynchronous_Job job_process_append(cz::Arc_Weak<Buffer_Handle> buffer_handle,
                                    cz::Process process,
                                    cz::Input_File output);

/// A job that joins the process when it completes and does nothing until that point.
Asynchronous_Job job_process_silent(cz::Process process);

namespace Run_Console_Command_Result_ {
enum Run_Console_Command_Result {
    FAILED,
    SUCCESS_NEW_BUFFER,
    SUCCESS_REUSE_BUFFER,
};
}
using Run_Console_Command_Result_::Run_Console_Command_Result;

Run_Console_Command_Result run_console_command(Client* client,
                                               Editor* editor,
                                               const char* working_directory,
                                               cz::Str script,
                                               cz::Str buffer_name,
                                               cz::Str error,
                                               cz::Arc<Buffer_Handle>* handle_out = nullptr);
Run_Console_Command_Result run_console_command(Client* client,
                                               Editor* editor,
                                               const char* working_directory,
                                               cz::Slice<cz::Str> args,
                                               cz::Str buffer_name,
                                               cz::Str error,
                                               cz::Arc<Buffer_Handle>* handle_out = nullptr);

bool run_console_command_in(Client* client,
                            Editor* editor,
                            cz::Arc<Buffer_Handle> buffer_id,
                            const char* working_directory,
                            cz::Str script,
                            cz::Str error);

}
