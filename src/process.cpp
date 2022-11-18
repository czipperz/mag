#include "process.hpp"

#include <cz/defer.hpp>
#include <cz/format.hpp>
#include <cz/process.hpp>
#include "client.hpp"

namespace mag {

bool run_process_for_output(Client* client,
                            cz::Slice<cz::Str> args,
                            cz::Str pretty_name,
                            const char* working_directory,
                            cz::Allocator allocator,
                            cz::String* out) {
    cz::Process process;
    cz::Input_File std_out;
    cz::Input_File std_err;
    CZ_DEFER(std_out.close());
    CZ_DEFER(std_err.close());
    {
        cz::Process_Options options;
        CZ_DEFER(options.std_out.close());
        CZ_DEFER(options.std_err.close());
        if (!create_process_output_pipe(&options.std_out, &std_out) ||
            create_process_output_pipe(&options.std_err, &std_err)) {
            auto msg = cz::format("Error: failed to create pipes for ", pretty_name);
            CZ_DEFER(msg.drop());
            client->show_message(msg);
            return false;
        }

        if (!process.launch_program(args, options)) {
            auto msg = cz::format("Error: failed to spawn ", pretty_name);
            CZ_DEFER(msg.drop());
            client->show_message(msg);
            return false;
        }
    }

    int exit_code = process.join();
    if (exit_code != 0) {
        auto msg = cz::format("Error: ", pretty_name, " failed with exit code: ", exit_code);
        CZ_DEFER(msg.drop());
        client->show_message(msg);
        return false;
    }

    cz::Heap_String err = {};
    CZ_DEFER(err.drop());
    if (!cz::read_to_string(std_err, cz::heap_allocator(), &err) || err.len > 0) {
        auto msg = cz::format("Error: ", pretty_name, " failed with stderr: ", err);
        CZ_DEFER(msg.drop());
        client->show_message(msg);
        return false;
    }

    (void)cz::read_to_string(std_out, allocator, out);
    return true;
}

}
