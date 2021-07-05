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

static bool man_completion_engine(Editor*, Completion_Engine_Context* context, bool) {
    cz::Str args[] = {path_to_autocomplete_man_page, ""};
    Run_Command_For_Completion_Results* runner = (Run_Command_For_Completion_Results*)context->data;
    cz::Process_Options options;
    return runner->iterate(context, args, options);
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
        client->show_message(editor, "Error: I/O operation failed");
        return;
    }
    CZ_DEFER(options.std_out.close());

    cz::Process process;
    if (!process.launch_script(script.buffer(), &options)) {
        client->show_message(editor, "Error: Couldn't show man page");
        stdout_read.close();
        return;
    }

    cz::String name = {};
    CZ_DEFER(name.drop(cz::heap_allocator()));
    name.reserve(cz::heap_allocator(), 4 + page.len);
    name.append("man ");
    name.append(page);

    cz::Arc<Buffer_Handle> handle = editor->create_temp_buffer(name);
    client->set_selected_buffer(handle->id);

    editor->add_asynchronous_job(
        job_process_append(handle.clone_downgrade(), process, stdout_read));
}

void command_man(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Man: ";
    dialog.completion_engine = man_completion_engine;
    dialog.response_callback = command_man_response;
    source.client->show_dialog(editor, dialog);

    auto data = cz::heap_allocator().alloc<Run_Command_For_Completion_Results>();
    *data = {};
    source.client->mini_buffer_completion_cache.engine_context.data = data;

    source.client->mini_buffer_completion_cache.engine_context.cleanup = [](void* _data) {
        auto data = (Run_Command_For_Completion_Results*)_data;
        data->drop();
        cz::heap_allocator().dealloc(data);
    };
}

}
}
