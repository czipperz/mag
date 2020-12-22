#include "git.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "file.hpp"
#include "message.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace git {

bool get_git_top_level(Client* client,
                       const char* dir_cstr,
                       cz::Allocator allocator,
                       cz::String* top_level_path) {
    cz::Input_File std_out_read;
    CZ_DEFER(std_out_read.close());

    cz::Process process;
    {
        cz::Process_Options options;
        options.working_directory = dir_cstr;

        if (!create_process_output_pipe(&options.std_out, &std_out_read)) {
            client->show_message("Error: I/O operation failed");
            return false;
        }
        options.std_err = options.std_out;
        CZ_DEFER(options.std_out.close());

        cz::Str rev_parse_args[] = {"git", "rev-parse", "--show-toplevel"};
        if (!process.launch_program(cz::slice(rev_parse_args), &options)) {
            client->show_message("No git repository found");
            return false;
        }
    }

    read_to_string(std_out_read, allocator, top_level_path);

    int return_value = process.join();
    if (return_value != 0) {
        client->show_message("No git repository found");
        return false;
    }

    CZ_DEBUG_ASSERT((*top_level_path)[top_level_path->len() - 1] == '\n');
    top_level_path->pop();
    top_level_path->null_terminate();
    return true;
}

void command_save_and_quit(Editor* editor, Command_Source source) {
    {
        WITH_SELECTED_BUFFER(source.client);
        if (!save_buffer(buffer)) {
            source.client->show_message("Error saving file");
            return;
        }
    }

    source.client->queue_quit = true;
}

void command_abort_and_quit(Editor* editor, Command_Source source) {
    {
        WITH_SELECTED_BUFFER(source.client);
        clear_buffer(buffer);

        if (!save_buffer(buffer)) {
            source.client->show_message("Error saving file");
            return;
        }
    }

    source.client->queue_quit = true;
}

}
}
