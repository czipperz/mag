#include "process.hpp"

#include <cz/defer.hpp>
#include <cz/format.hpp>
#include <cz/process.hpp>
#include "core/client.hpp"

namespace mag {

bool run_process_for_output(Client* client,
                            cz::Slice<cz::Str> args,
                            cz::Str pretty_name,
                            const char* working_directory,
                            cz::Heap_String* out) {
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
            !create_process_output_pipe(&options.std_err, &std_err)) {
            client->show_message_format("Error: failed to create pipes for ", pretty_name);
            return false;
        }

        options.working_directory = working_directory;

        if (!process.launch_program(args, options)) {
            client->show_message_format("Error: failed to spawn ", pretty_name);
            return false;
        }
    }

    int exit_code = process.join();

    cz::Heap_String err = {};
    CZ_DEFER(err.drop());
    if (!cz::read_to_string(std_err, cz::heap_allocator(), &err) || err.len > 0) {
        client->show_message_format("Error: ", pretty_name, " failed with stderr: ", err);
        return false;
    }

    if (exit_code != 0) {
        client->show_message_format("Error: ", pretty_name, " failed with exit code: ", exit_code);
        return false;
    }

    (void)cz::read_to_string(std_out, cz::heap_allocator(), out);
    return true;
}

int run_process_for_output(cz::Slice<cz::Str> args,
                           cz::Str pretty_name,
                           const char* working_directory,
                           cz::Heap_String* out,
                           cz::Heap_String* err) {
    cz::Process process;
    cz::Input_File std_out;
    cz::Input_File std_err;
    CZ_DEFER(std_out.close());
    CZ_DEFER(std_err.close());
    {
        cz::Process_Options options;
        CZ_DEFER(options.std_out.close());
        CZ_DEFER(options.std_err.close());
        if (out && !create_process_output_pipe(&options.std_out, &std_out))
            return 128;

        if (err && !create_process_output_pipe(&options.std_err, &std_err)) {
            cz::append(err, "Error: failed to create pipes for ", pretty_name);
            return 128;
        }

        options.working_directory = working_directory;

        if (!process.launch_program(args, options)) {
            if (err)
                cz::append(err, "Error: failed to spawn ", pretty_name);
            return 128;
        }
    }

    if (out)
        (void)cz::read_to_string(std_out, cz::heap_allocator(), out);
    if (err)
        (void)cz::read_to_string(std_err, cz::heap_allocator(), err);
    return process.join();
}

}
