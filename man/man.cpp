#include "man.hpp"

#include <algorithm>
#include <cz/buffer_array.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include "client.hpp"
#include "command.hpp"
#include "editor.hpp"
#include "process.hpp"

namespace mag {
namespace man {

const char* path_to_autocomplete_man_page;
const char* path_to_load_man_page;

static void man_completion_engine(Editor*, Completion_Engine_Context* context) {
    if (context->results.len() > 0) {
        return;
    }

    Process process;
    const char* args[] = {path_to_autocomplete_man_page, "", nullptr};
    if (!process.launch_program(path_to_autocomplete_man_page, args, nullptr)) {
        return;
    }
    process.set_read_blocking();

    context->results_buffer_array.clear();
    context->results.set_len(0);

    char buffer[1024];
    cz::String result = {};
    while (1) {
        int64_t len = process.read(buffer, sizeof(buffer));
        if (len > 0) {
            for (size_t offset = 0; offset < (size_t)len; ++offset) {
                const char* end = cz::Str{buffer + offset, len - offset}.find('\n');

                size_t rlen;
                if (end) {
                    rlen = end - buffer - offset;
                } else {
                    rlen = len - offset;
                }

                result.reserve(context->results_buffer_array.allocator(), rlen);
                result.append({buffer + offset, rlen});

                if (!end) {
                    break;
                }

                context->results.reserve(cz::heap_allocator(), 1);
                context->results.push(result);
                result = {};
                offset += rlen;
            }
        } else {
            break;
        }
    }

    process.join();
    process.destroy();

    std::sort(context->results.start(), context->results.end());
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

    Process process;
    if (!process.launch_script(script.buffer(), nullptr)) {
        client->show_message("Error: Couldn't show man page");
        return;
    }

    Buffer_Id buffer_id = editor->create_temp_buffer("man");
    client->set_selected_buffer(buffer_id);
    editor->add_job(job_process_append(buffer_id, process));
}

void command_man(Editor* editor, Command_Source source) {
    source.client->show_dialog(editor, "Man: ", man_completion_engine, command_man_response,
                               nullptr);
}

}
}
