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

struct Man_Completion_Engine_Data {
    cz::Vector<cz::Str> all_results;
    size_t offset;
};

static void man_completion_engine_data_cleanup(void* _data) {
    Man_Completion_Engine_Data* data = (Man_Completion_Engine_Data*)_data;
    data->all_results.drop(cz::heap_allocator());
    free(data);
}

static void man_completion_engine(Editor* _editor, Completion_Results* completion_results) {
    if (!completion_results->data) {
        completion_results->data = calloc(1, sizeof(Man_Completion_Engine_Data));
        CZ_ASSERT(completion_results->data);
        completion_results->cleanup = man_completion_engine_data_cleanup;

        Man_Completion_Engine_Data* data = (Man_Completion_Engine_Data*)completion_results->data;

        Process process;
        const char* args[] = {path_to_autocomplete_man_page, "", nullptr};
        if (!process.launch_program(path_to_autocomplete_man_page, args, nullptr)) {
            completion_results->state = Completion_Results::LOADED;
            return;
        }
        process.set_read_blocking();

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

                    result.reserve(completion_results->results_buffer_array.allocator(), rlen);
                    result.append({buffer + offset, rlen});

                    if (!end) {
                        break;
                    }

                    data->all_results.reserve(cz::heap_allocator(), 1);
                    data->all_results.push(result);
                    result = {};
                    offset += rlen;
                }
            } else {
                break;
            }
        }

        process.join();
        process.destroy();

        std::sort(data->all_results.start(), data->all_results.end());
    }

    Man_Completion_Engine_Data* data = (Man_Completion_Engine_Data*)completion_results->data;
    completion_results->selected += data->offset;

    completion_results->results.set_len(0);
    size_t start;
    if (binary_search_string_prefix_start(data->all_results, completion_results->query, &start)) {
        size_t end =
            binary_search_string_prefix_end(data->all_results, start, completion_results->query);
        completion_results->results.reserve(cz::heap_allocator(), end - start);
        completion_results->results.append({data->all_results.elems() + start, end - start});

        data->offset = start;
        if (completion_results->selected >= start && completion_results->selected < end) {
            completion_results->selected -= start;
        } else {
            completion_results->selected = 0;
        }
    } else {
        data->offset = 0;
        completion_results->selected = 0;
    }

    completion_results->state = Completion_Results::LOADED;
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
