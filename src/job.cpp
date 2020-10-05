#include "job.hpp"

#include <cz/defer.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "process.hpp"

namespace mag {

struct Process_Append_Job_Data {
    Buffer_Id buffer_id;
    Process process;
};

static void process_append_job_kill(Editor* editor, void* _data) {
    Process_Append_Job_Data* data = (Process_Append_Job_Data*)_data;
    data->process.kill();
    data->process.destroy();
    free(data);
}

static bool process_append_job_tick(Editor* editor, void* _data) {
    Process_Append_Job_Data* data = (Process_Append_Job_Data*)_data;
    char buf[1024];
    int64_t read_result = data->process.read(buf, sizeof(buf));
    if (read_result > 0) {
        Buffer_Handle* handle = editor->lookup(data->buffer_id);
        if (!handle) {
            process_append_job_kill(editor, data);
            return true;
        }

        Buffer* buffer = handle->lock();
        CZ_DEFER(handle->unlock());
        buffer->contents.append({buf, (size_t)read_result});
        return false;
    } else if (read_result == 0) {
        // End of file
        data->process.join();
        data->process.destroy();
        free(data);
        return true;
    } else {
        // Nothing to read right now
        return false;
    }
}

Job job_process_append(Buffer_Id buffer_id, Process process) {
    Process_Append_Job_Data* data =
        (Process_Append_Job_Data*)malloc(sizeof(Process_Append_Job_Data));
    CZ_ASSERT(data);
    data->buffer_id = buffer_id;
    data->process = process;

    Job job;
    job.tick = process_append_job_tick;
    job.kill = process_append_job_kill;
    job.data = data;
    return job;
}

bool run_console_command(Client* client,
                         Editor* editor,
                         const char* working_directory,
                         const char* script,
                         cz::Str buffer_name,
                         cz::Str error) {
    Process process;
    if (!process.launch_script(script, working_directory)) {
        client->show_message(error);
        return false;
    }

    Buffer_Id buffer_id = editor->create_temp_buffer(buffer_name);
    {
        WITH_BUFFER(buffer_id);
        buffer->contents.insert(0, working_directory);
        buffer->contents.insert(buffer->contents.len, ": ");
        buffer->contents.insert(buffer->contents.len, script);
        buffer->contents.insert(buffer->contents.len, "\n");
    }

    client->set_selected_buffer(buffer_id);

    editor->add_job(job_process_append(buffer_id, process));
    return true;
}

}
