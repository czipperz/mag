#include "job.hpp"

#include <cz/arc.hpp>
#include <cz/defer.hpp>
#include <cz/format.hpp>
#include <cz/process.hpp>
#include <cz/util.hpp>
#include <cz/working_directory.hpp>
#include "core/client.hpp"
#include "core/command_macros.hpp"
#include "core/editor.hpp"
#include "core/file.hpp"

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

Asynchronous_Job Asynchronous_Job::do_nothing() {
    Asynchronous_Job job;
    job.tick = [](Asynchronous_Job_Handler*, void*) { return Job_Tick_Result::FINISHED; };
    job.kill = [](void*) {};
    job.data = nullptr;
    return job;
}

Synchronous_Job Synchronous_Job::do_nothing() {
    Synchronous_Job job;
    job.tick = [](Editor*, Client*, void*) { return Job_Tick_Result::FINISHED; };
    job.kill = [](void*) {};
    job.data = nullptr;
    return job;
}

////////////////////////////////////////////////////////////////////////////////
// Job show message once no prompt
////////////////////////////////////////////////////////////////////////////////

static void job_show_message_once_no_prompt_kill(void* _data) {
    cz::String* data = (cz::String*)_data;
    data->drop(cz::heap_allocator());
    cz::heap_allocator().dealloc(data);
}

static Job_Tick_Result job_show_message_once_no_prompt_tick(Editor* editor,
                                                            Client* client,
                                                            void* _data) {
    if (client->_message.tag == Message::NONE) {
        cz::String* data = (cz::String*)_data;
        client->show_message(*data);
        job_show_message_once_no_prompt_kill(data);
        return Job_Tick_Result::FINISHED;
    } else {
        return Job_Tick_Result::STALLED;
    }
}

static Synchronous_Job job_show_message_once_no_prompt(cz::String message) {
    cz::String* data = cz::heap_allocator().clone(message);
    CZ_ASSERT(data);
    Synchronous_Job job;
    job.data = data;
    job.tick = job_show_message_once_no_prompt_tick;
    job.kill = job_show_message_once_no_prompt_kill;
    return job;
}

////////////////////////////////////////////////////////////////////////////////
// Job process append
////////////////////////////////////////////////////////////////////////////////

struct Process_Append_Job_Data {
    cz::Arc_Weak<Buffer_Handle> buffer_handle;
    cz::Process process;
    cz::Carriage_Return_Carry carry;
    cz::Input_File std_out;
    Synchronous_Job callback;
};

static void process_append_job_kill(void* _data) {
    Process_Append_Job_Data* data = (Process_Append_Job_Data*)_data;
    data->std_out.close();
    data->process.kill();
    data->buffer_handle.drop();
    (*data->callback.kill)(data->callback.data);
    cz::heap_allocator().dealloc(data);
}

static Job_Tick_Result process_append_job_tick(Asynchronous_Job_Handler* handler, void* _data) {
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

            // Show a message that the command finished.
            {
                cz::Arc<Buffer_Handle> handle;
                if (!data->buffer_handle.upgrade(&handle)) {
                    process_append_job_kill(data);
                    return Job_Tick_Result::FINISHED;
                }
                CZ_DEFER(handle.drop());

                cz::String message = {};
                CZ_DEFER(message.drop(cz::heap_allocator()));
                {
                    WITH_CONST_BUFFER_HANDLE(handle);
                    message = cz::format("Finished: ", buffer->name);
                }
                handler->add_synchronous_job(job_show_message_once_no_prompt(message));
                message = {};
            }

            // Cleanup.
            data->std_out.close();
            data->process.join();
            data->buffer_handle.drop();
            handler->add_synchronous_job(data->callback);
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
                                    cz::Input_File std_out,
                                    Synchronous_Job callback) {
    Process_Append_Job_Data* data = cz::heap_allocator().alloc<Process_Append_Job_Data>();
    CZ_ASSERT(data);
    data->buffer_handle = buffer_handle;
    data->process = process;
    data->carry = {};
    data->std_out = std_out;
    data->callback = callback;

    Asynchronous_Job job;
    job.tick = process_append_job_tick;
    job.kill = process_append_job_kill;
    job.data = data;
    return job;
}

////////////////////////////////////////////////////////////////////////////////
// Job process silent
////////////////////////////////////////////////////////////////////////////////

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

////////////////////////////////////////////////////////////////////////////////
// Job process stderr show message
////////////////////////////////////////////////////////////////////////////////

struct Process_Show_Message_With_File_Contents_Job {
    cz::Process process;
    cz::Carriage_Return_Carry carry;
    cz::Input_File file;
    cz::String string;
    size_t prefix_length;
};

static void process_show_message_with_file_contents_job_kill(void* _data) {
    Process_Show_Message_With_File_Contents_Job* data =
        (Process_Show_Message_With_File_Contents_Job*)_data;
    data->process.kill();
    data->file.close();
    data->string.drop(cz::heap_allocator());
    cz::heap_allocator().dealloc(data);
}

static Job_Tick_Result process_show_message_with_file_contents_job_tick(
    Asynchronous_Job_Handler* handler,
    void* _data) {
    Process_Show_Message_With_File_Contents_Job* data =
        (Process_Show_Message_With_File_Contents_Job*)_data;

    (void)cz::read_to_string(data->file, cz::heap_allocator(), &data->string);

    int ret;
    if (data->process.try_join(&ret)) {
        if (data->string.len > data->prefix_length) {
            size_t newline = data->string.find_index('\n');
            handler->show_message(data->string.slice_end(newline));
        }

        data->file.close();
        data->string.drop(cz::heap_allocator());
        cz::heap_allocator().dealloc(data);
        return Job_Tick_Result::FINISHED;
    } else {
        return Job_Tick_Result::STALLED;
    }
}

Asynchronous_Job job_process_show_message_with_file_contents(cz::Process process,
                                                             cz::Input_File file,
                                                             cz::Str message_prefix) {
    Process_Show_Message_With_File_Contents_Job* data =
        cz::heap_allocator().alloc<Process_Show_Message_With_File_Contents_Job>();
    CZ_ASSERT(data);
    data->process = process;
    data->carry = {};
    data->file = file;
    data->string = {};
    data->string.reserve(cz::heap_allocator(), cz::max((size_t)1024, message_prefix.len));
    data->string.append(message_prefix);
    data->prefix_length = message_prefix.len;

    Asynchronous_Job job;
    job.tick = process_show_message_with_file_contents_job_tick;
    job.kill = process_show_message_with_file_contents_job_kill;
    job.data = data;
    return job;
}

////////////////////////////////////////////////////////////////////////////////
// Run console command
////////////////////////////////////////////////////////////////////////////////

Run_Console_Command_Result run_console_command(Client* client,
                                               Editor* editor,
                                               const char* working_directory,
                                               cz::Str script,
                                               cz::Str buffer_name,
                                               cz::Arc<Buffer_Handle>* handle_out,
                                               Synchronous_Job callback) {
    ZoneScoped;

    cz::String working_directory_storage = {};
    CZ_DEFER(working_directory_storage.drop(cz::heap_allocator()));
    if (!working_directory) {
        if (!cz::get_working_directory(cz::heap_allocator(), &working_directory_storage)) {
            client->show_message("Failed to get working directory");
            (*callback.kill)(callback.data);
            return Run_Console_Command_Result::FAILED;
        }
        working_directory = working_directory_storage.buffer;
    }

    {
        WITH_CONST_SELECTED_BUFFER(client);
        push_jump(window, client, buffer);
    }

    bool created = false;
    cz::Arc<Buffer_Handle> handle;
    if (!find_temp_buffer(editor, client, buffer_name, cz::Str{working_directory}, &handle)) {
        handle = editor->create_buffer(create_temp_buffer(buffer_name, cz::Str{working_directory}));
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

    client->select_window_for_buffer_or_replace_current(handle);

    if (!run_console_command_in(client, editor, handle, working_directory, script, callback)) {
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
                            Synchronous_Job callback) {
    ZoneScoped;

    cz::Process_Options options;
    options.working_directory = working_directory;
#ifdef _WIN32
    options.hide_window = true;
#endif

    cz::Input_File stdout_read;
    if (!create_process_output_pipe(&options.std_out, &stdout_read)) {
        client->show_message("Error: I/O operation failed");
        (*callback.kill)(callback.data);
        return false;
    }
    stdout_read.set_non_blocking();
    CZ_DEFER(options.std_out.close());

    options.std_err = options.std_out;

    cz::Process process;
    if (!process.launch_script(script, options)) {
        client->show_message_format("Failed to run: ", script);
        stdout_read.close();
        (*callback.kill)(callback.data);
        return false;
    }

    editor->add_asynchronous_job(
        job_process_append(handle.clone_downgrade(), process, stdout_read, callback));
    return true;
}

Run_Console_Command_Result run_console_command(Client* client,
                                               Editor* editor,
                                               const char* working_directory,
                                               cz::Slice<cz::Str> args,
                                               cz::Str buffer_name,
                                               cz::Arc<Buffer_Handle>* handle_out,
                                               Synchronous_Job callback) {
    cz::String script = {};
    CZ_DEFER(script.drop(cz::heap_allocator()));
    cz::Process::escape_args(args, &script, cz::heap_allocator());
    return run_console_command(client, editor, working_directory, script.buffer, buffer_name,
                               handle_out, callback);
}

}
