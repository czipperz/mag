#include "javascript_commands.hpp"

#include <cz/format.hpp>
#include <cz/process.hpp>
#include "core/command.hpp"
#include "core/command_macros.hpp"
#include "core/diff.hpp"
#include "core/editor.hpp"
#include "core/file.hpp"
#include "core/movement.hpp"

namespace mag {
namespace javascript {

struct Job_Jq {
    cz::Process process;

    cz::Input_File err_in;
    cz::String err_string;
    cz::Carriage_Return_Carry err_carry;

    cz::String output_path;
    cz::Arc_Weak<Buffer_Handle> buffer;
};

static void do_cleanup(Job_Jq* data) {
    data->err_in.close();
    data->err_string.drop(cz::heap_allocator());
    data->output_path.drop(cz::heap_allocator());
    data->buffer.drop();
    cz::heap_allocator().dealloc(data);
}

static bool commit_reformat(Asynchronous_Job_Handler* job_handler,
                            cz::Arc_Weak<Buffer_Handle> weak,
                            const char* output_path) {
    cz::Arc<Buffer_Handle> handle;
    if (!weak.upgrade(&handle))
        return false;
    CZ_DEFER(handle.drop());

    WITH_BUFFER_HANDLE(handle);

    // Move temp file to buffer path.
    {
        cz::String buffer_path = {};
        if (!buffer->get_path(cz::heap_allocator(), &buffer_path))
            return false;
        CZ_DEFER(buffer_path.drop(cz::heap_allocator()));

        if (!cz::file::rename_file(output_path, buffer_path.buffer)) {
            if (!cz::file::remove_file(buffer_path.buffer)) {
                job_handler->show_message("Couldn't remove destination");
                return false;
            }
            if (!cz::file::rename_file(output_path, buffer_path.buffer)) {
                job_handler->show_message("Couldn't rename path");
                return false;
            }
        }
    }

    // Rely on auto refresh feature to load diff.
    const char* message = reload_file(buffer);
    if (message) {
        job_handler->show_message(message);
        return false;
    }
    return true;
}

static Job_Tick_Result do_tick(Asynchronous_Job_Handler* handler, void* _data) {
    Job_Jq* data = (Job_Jq*)_data;

    // Read more of the error message.
    while (1) {
        data->err_string.reserve(cz::heap_allocator(), 1024);
        int64_t result = data->err_in.read_text(data->err_string.end(),
                                                data->err_string.remaining(), &data->err_carry);
        if (result <= 0)
            break;

        data->err_string.len += result;

        size_t end = data->err_string.find_index('\n');
        if (end <= 1024 && end >= data->err_string.len - result) {
            // If the error changes, show it to the user.
            // TODO: rate limit to avoid blocking user.
            // TODO: parse errors to show more simply.
            handler->show_message(data->err_string.slice_end(end));
        }
    }

    int ret = 0;
    if (data->process.try_join(&ret)) {
        // If jq fails with no std_err output then log a message so the user knows.
        if (ret != 0 && data->err_string.len == 0) {
            handler->show_message_format("jq failed with error code: ", ret);
        }

        // If jq succeeds then push the string in.
        if (ret == 0) {
            commit_reformat(handler, data->buffer, data->output_path.buffer);
        }

        do_cleanup(data);
        return Job_Tick_Result::FINISHED;
    } else {
        return Job_Tick_Result::STALLED;
    }
}

static void do_kill(void* _data) {
    Job_Jq* data = (Job_Jq*)_data;
    data->process.kill();
    do_cleanup(data);
}

static Asynchronous_Job job_jq_handle_result(const Job_Jq& state) {
    Job_Jq* data = cz::heap_allocator().clone(state);
    CZ_ASSERT(data);

    Asynchronous_Job job;
    job.tick = do_tick;
    job.kill = do_kill;
    job.data = data;
    return job;
}

REGISTER_COMMAND(command_jq_format_buffer);
void command_jq_format_buffer(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    Job_Jq state = {};
    CZ_DEFER(state.err_in.close());
    CZ_DEFER(state.output_path.drop(cz::heap_allocator()));

    {
        cz::String buffer_path = {};
        CZ_DEFER(buffer_path.drop(cz::heap_allocator()));
        if (!buffer->get_path(cz::heap_allocator(), &buffer_path)) {
            source.client->show_message("Error: buffer isn't backed by a file");
            return;
        }

        cz::Input_File temp_input;
        CZ_DEFER(temp_input.close());
        if (!save_buffer_to_temp_file(buffer, &temp_input)) {
            source.client->show_message("Error: couldn't save buffer to temp file");
            return;
        }

        char temp_output_file[L_tmpnam];
        if (!tmpnam(temp_output_file)) {
            source.client->show_message("Error: failed to find a temp file path");
            return;
        }
        state.output_path = cz::format(cz::heap_allocator(), temp_output_file);

        cz::Output_File temp_output;
        CZ_DEFER(temp_output.close());
        if (!temp_output.open(temp_output_file)) {
            source.client->show_message("Error: couldn't open temp output file");
            return;
        }

        cz::Output_File err_out;
        CZ_DEFER(err_out.close());
        if (!cz::create_process_output_pipe(&err_out, &state.err_in)) {
            source.client->show_message("Error: couldn't open pipe");
            return;
        }

        cz::Process_Options options;
        options.working_directory = buffer->directory.buffer;
        options.std_in = temp_input;
        options.std_out = temp_output;
        options.std_err = err_out;

        cz::Str args[] = {"jq", "."};
        if (!state.process.launch_program(args, options)) {
            source.client->show_message("Shell error");
            return;
        }
    }

    state.buffer = handle.clone_downgrade();
    editor->add_asynchronous_job(job_jq_handle_result(state));
    state = {};
}

}
}
