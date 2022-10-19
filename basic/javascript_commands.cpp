#include "javascript_commands.hpp"

#include <cz/process.hpp>
#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "movement.hpp"

namespace mag {
namespace javascript {

REGISTER_COMMAND(command_jq_format_buffer);
void command_jq_format_buffer(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    cz::String buffer_path = {};
    CZ_DEFER(buffer_path.drop(cz::heap_allocator()));
    if (!buffer->get_path(cz::heap_allocator(), &buffer_path)) {
        source.client->show_message("Error: buffer isn't backed by a file");
        return;
    }

    cz::Input_File temp_file;
    CZ_DEFER(temp_file.close());
    if (!save_buffer_to_temp_file(buffer, &temp_file)) {
        source.client->show_message("Error: couldn't save buffer to temp file");
        return;
    }

    cz::Output_File out_file;
    CZ_DEFER(out_file.close());
    if (!out_file.open(buffer_path.buffer)) {
        source.client->show_message("Error: couldn't open output file");
        return;
    }

    cz::Process_Options options;
    options.working_directory = buffer->directory.buffer;
    options.std_in = temp_file;
    options.std_out = out_file;

    cz::Process process;
    cz::Str args[] = {"jq"};
    if (!process.launch_program(args, options)) {
        source.client->show_message("Shell error");
        return;
    }

    editor->add_asynchronous_job(job_process_silent(process));
}

}
}
