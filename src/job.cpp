#include "job.hpp"

#include <cz/arc.hpp>
#include <cz/defer.hpp>
#include <cz/process.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "editor.hpp"

namespace mag {

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
    free(data);
}

static bool process_append_job_tick(void* _data) {
    Process_Append_Job_Data* data = (Process_Append_Job_Data*)_data;
    char buf[1024];
    int64_t read_result = data->std_out.read_text(buf, sizeof(buf), &data->carry);
    if (read_result > 0) {
        cz::Arc<Buffer_Handle> handle;
        if (!data->buffer_handle.upgrade(&handle)) {
            process_append_job_kill(data);
            return true;
        }
        CZ_DEFER(handle.drop());

        Buffer* buffer = handle->lock_writing();
        CZ_DEFER(handle->unlock());
        buffer->contents.append({buf, (size_t)read_result});
        return false;
    } else if (read_result == 0) {
        // End of file
        data->std_out.close();
        data->process.join();
        free(data);
        return true;
    } else {
        // Nothing to read right now
        return false;
    }
}

Job job_process_append(cz::Arc_Weak<Buffer_Handle> buffer_handle,
                       cz::Process process,
                       cz::Input_File std_out) {
    Process_Append_Job_Data* data =
        (Process_Append_Job_Data*)malloc(sizeof(Process_Append_Job_Data));
    CZ_ASSERT(data);
    data->buffer_handle = buffer_handle;
    data->process = process;
    data->carry = {};
    data->std_out = std_out;

    Job job;
    job.tick = process_append_job_tick;
    job.kill = process_append_job_kill;
    job.data = data;
    return job;
}

bool run_console_command(Client* client,
                         Editor* editor,
                         const char* working_directory,
                         cz::Str script,
                         cz::Str buffer_name,
                         cz::Str error) {
    cz::Option<cz::Str> wd = {};
    if (working_directory) {
        wd = {working_directory};
    }

    Buffer_Id buffer_id = editor->create_temp_buffer(buffer_name, wd);
    {
        WITH_BUFFER(buffer_id);
        buffer->contents.append(script);
        buffer->contents.append("\n");
    }
    client->set_selected_buffer(buffer_id);

    return run_console_command_in(client, editor, buffer_id, working_directory, script, error);
}

bool run_console_command_in(Client* client,
                            Editor* editor,
                            Buffer_Id buffer_id,
                            const char* working_directory,
                            cz::Str script,
                            cz::Str error) {
    cz::Process_Options options;
    options.working_directory = working_directory;

    cz::Input_File stdout_read;
    if (!create_process_output_pipe(&options.std_out, &stdout_read)) {
        client->show_message("Error: I/O operation failed");
        return false;
    }
    CZ_DEFER(options.std_out.close());

    cz::Process process;
    if (!process.launch_script(script, &options)) {
        client->show_message(error);
        stdout_read.close();
        return false;
    }

    cz::Arc<Buffer_Handle> buffer_handle;
    if (!editor->lookup(buffer_id, &buffer_handle)) {
        CZ_PANIC("Buffer was deleted while we were using it");
    }

    editor->add_job(job_process_append(buffer_handle.clone_downgrade(), process, stdout_read));
    return true;
}

bool run_console_command(Client* client,
                         Editor* editor,
                         const char* working_directory,
                         cz::Slice<cz::Str> args,
                         cz::Str buffer_name,
                         cz::Str error) {
    cz::String script = {};
    CZ_DEFER(script.drop(cz::heap_allocator()));
    cz::Process::escape_args(args, &script, cz::heap_allocator());
    return run_console_command(client, editor, working_directory, script.buffer(), buffer_name,
                               error);
}

}
