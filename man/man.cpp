#include "man.hpp"

#include <algorithm>
#include <cz/buffer_array.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include "client.hpp"
#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace man {

const char* path_to_autocomplete_man_page;
const char* path_to_load_man_page;

static void man_completion_engine(Editor*, Completion_Engine_Context* context) {
    const char* args[] = {path_to_autocomplete_man_page, "", nullptr};
    cz::Process_Options options;
    run_command_for_completion_results(context, args, options);
}

static void command_man_response(Editor* editor, Client* client, cz::Str page, void* data) {
    // $load $page | groff -Tascii -man
    cz::Str load = path_to_load_man_page;
    cz::Str pipe_to_groff = " | groff -Tascii -man";

    cz::String script = {};
    CZ_DEFER(script.drop(cz::heap_allocator()));
    script.reserve(cz::heap_allocator(), load.len + page.len + pipe_to_groff.len + 2);
    script.append(load);
    script.push(' ');
    script.append(page);
    script.append(pipe_to_groff);
    script.null_terminate();

    cz::Process_Options options;
    cz::Input_File stdout_read;
    if (!create_process_output_pipe(&options.std_out, &stdout_read)) {
        client->show_message("Error: I/O operation failed");
        return;
    }
    CZ_DEFER(options.std_out.close());

    cz::Process process;
    if (!process.launch_script(script.buffer(), &options)) {
        client->show_message("Error: Couldn't show man page");
        stdout_read.close();
        return;
    }

    Buffer_Id buffer_id = editor->create_temp_buffer("man");
    client->set_selected_buffer(buffer_id);
    editor->add_job(job_process_append(buffer_id, process, stdout_read));
}

void command_man(Editor* editor, Command_Source source) {
    source.client->show_dialog(editor, "Man: ", man_completion_engine, command_man_response,
                               nullptr);
}

}
}
