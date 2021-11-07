#include "job.hpp"

#include <cz/arc.hpp>
#include <cz/defer.hpp>
#include <cz/process.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "file.hpp"

namespace mag {

void Asynchronous_Job_Handler::add_synchronous_job(Synchronous_Job job) {
    pending_synchronous_jobs.reserve(cz::heap_allocator(), 1);
    pending_synchronous_jobs.push(job);
}

void Asynchronous_Job_Handler::add_asynchronous_job(Asynchronous_Job job) {
    pending_asynchronous_jobs.reserve(cz::heap_allocator(), 1);
    pending_asynchronous_jobs.push(job);
}

struct Show_Message_Job_Data {
    cz::String message;
};

static void show_message_job_kill(void* _data) {
    Show_Message_Job_Data* data = (Show_Message_Job_Data*)_data;
    data->message.drop(cz::heap_allocator());
}

static Job_Tick_Result show_message_job_tick(Editor* editor, Client* client, void* _data) {
    Show_Message_Job_Data* data = (Show_Message_Job_Data*)_data;
    client->show_message(data->message);
    data->message.drop(cz::heap_allocator());
    return Job_Tick_Result::FINISHED;
}

void Asynchronous_Job_Handler::show_message(cz::Str message) {
    Show_Message_Job_Data* data = cz::heap_allocator().alloc<Show_Message_Job_Data>();
    CZ_ASSERT(data);
    data->message = message.clone(cz::heap_allocator());

    Synchronous_Job job;
    job.tick = show_message_job_tick;
    job.kill = show_message_job_kill;
    job.data = data;
    add_synchronous_job(job);
}

struct Process_Append_Job_Data {
    cz::Arc_Weak<Buffer_Handle> buffer_handle;
    cz::Process process;
    cz::Carriage_Return_Carry carry;
    cz::Input_File std_out;
};

static void process_append_job_kill(void* _data) {
    Process_Append_Job_Data* data = (Process_Append_Job_Data*)_data;
    data->std_out.close();
    data->process.kill();
    data->buffer_handle.drop();
    cz::heap_allocator().dealloc(data);
}

static Job_Tick_Result process_append_job_tick(Asynchronous_Job_Handler*, void* _data) {
    ZoneScoped;

    Process_Append_Job_Data* data = (Process_Append_Job_Data*)_data;
    char buf[1024];
    for (int reads = 0; reads < 128; ++reads) {
        int64_t read_result = data->std_out.read_text(buf, sizeof(buf), &data->carry);
        if (read_result > 0) {
            cz::Arc<Buffer_Handle> handle;
            if (!data->buffer_handle.upgrade(&handle)) {
                process_append_job_kill(data);
                return Job_Tick_Result::FINISHED;
            }
            CZ_DEFER(handle.drop());

            WITH_BUFFER_HANDLE(handle);
            buffer->contents.append({buf, (size_t)read_result});
            continue;
        } else if (read_result == 0) {
            // End of file
            data->std_out.close();
            data->process.join();
            data->buffer_handle.drop();
            cz::heap_allocator().dealloc(data);
            return Job_Tick_Result::FINISHED;
        } else {
            // Nothing to read right now
            return reads > 0 ? Job_Tick_Result::MADE_PROGRESS : Job_Tick_Result::STALLED;
        }
    }

    // Let another job run.
    return Job_Tick_Result::MADE_PROGRESS;
}

Asynchronous_Job job_process_append(cz::Arc_Weak<Buffer_Handle> buffer_handle,
                                    cz::Process process,
                                    cz::Input_File std_out) {
    Process_Append_Job_Data* data = cz::heap_allocator().alloc<Process_Append_Job_Data>();
    CZ_ASSERT(data);
    data->buffer_handle = buffer_handle;
    data->process = process;
    data->carry = {};
    data->std_out = std_out;

    Asynchronous_Job job;
    job.tick = process_append_job_tick;
    job.kill = process_append_job_kill;
    job.data = data;
    return job;
}

struct Process_Silent_Job_Data {
    cz::Process process;
};

static void process_silent_job_kill(void* _data) {
    Process_Silent_Job_Data* data = (Process_Silent_Job_Data*)_data;
    data->process.kill();
    cz::heap_allocator().dealloc(data);
}

static Job_Tick_Result process_silent_job_tick(Asynchronous_Job_Handler*, void* _data) {
    Process_Silent_Job_Data* data = (Process_Silent_Job_Data*)_data;
    int ret;
    if (data->process.try_join(&ret)) {
        cz::heap_allocator().dealloc(data);
        return Job_Tick_Result::FINISHED;
    } else {
        return Job_Tick_Result::STALLED;
    }
}

Asynchronous_Job job_process_silent(cz::Process process) {
    Process_Silent_Job_Data* data = cz::heap_allocator().alloc<Process_Silent_Job_Data>();
    CZ_ASSERT(data);
    data->process = process;

    Asynchronous_Job job;
    job.tick = process_silent_job_tick;
    job.kill = process_silent_job_kill;
    job.data = data;
    return job;
}

Run_Console_Command_Result run_console_command(Client* client,
                                               Editor* editor,
                                               const char* working_directory,
                                               cz::Str script,
                                               cz::Str buffer_name,
                                               cz::Str error,
                                               cz::Arc<Buffer_Handle>* handle_out) {
    ZoneScoped;

    cz::Option<cz::Str> wd = {};
    if (working_directory) {
        wd = {working_directory};
    }

    {
        WITH_CONST_SELECTED_BUFFER(client);
        push_jump(window, client, buffer);
    }

    bool created = false;
    cz::Arc<Buffer_Handle> handle;
    if (!find_temp_buffer(editor, client, buffer_name, wd, &handle)) {
        handle = editor->create_temp_buffer(buffer_name, wd);
        created = true;
    }

    if (handle_out) {
        *handle_out = handle;
    }

    {
        WITH_BUFFER_HANDLE(handle);
        buffer->contents.remove(0, buffer->contents.len);
        buffer->contents.append(script);
        buffer->contents.append("\n");
    }

    client->set_selected_buffer(handle);

    if (!run_console_command_in(client, editor, handle, working_directory, script, error)) {
        return Run_Console_Command_Result::FAILED;
    }

    return created ? Run_Console_Command_Result::SUCCESS_NEW_BUFFER
                   : Run_Console_Command_Result::SUCCESS_REUSE_BUFFER;
}

bool run_console_command_in(Client* client,
                            Editor* editor,
                            cz::Arc<Buffer_Handle> handle,
                            const char* working_directory,
                            cz::Str script,
                            cz::Str error) {
    ZoneScoped;

    cz::Process_Options options;
    options.working_directory = working_directory;
#ifdef _WIN32
    options.hide_window = true;
#endif

    cz::Input_File stdout_read;
    if (!create_process_output_pipe(&options.std_out, &stdout_read)) {
        client->show_message("Error: I/O operation failed");
        return false;
    }
    stdout_read.set_non_blocking();
    CZ_DEFER(options.std_out.close());

    options.std_err = options.std_out;

    cz::Process process;
    if (!process.launch_script(script, options)) {
        client->show_message(error);
        stdout_read.close();
        return false;
    }

    editor->add_asynchronous_job(
        job_process_append(handle.clone_downgrade(), process, stdout_read));
    return true;
}

Run_Console_Command_Result run_console_command(Client* client,
                                               Editor* editor,
                                               const char* working_directory,
                                               cz::Slice<cz::Str> args,
                                               cz::Str buffer_name,
                                               cz::Str error,
                                               cz::Arc<Buffer_Handle>* handle_out) {
    cz::String script = {};
    CZ_DEFER(script.drop(cz::heap_allocator()));
    cz::Process::escape_args(args, &script, cz::heap_allocator());
    return run_console_command(client, editor, working_directory, script.buffer, buffer_name, error,
                               handle_out);
}

}
