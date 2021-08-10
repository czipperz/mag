#include "xclip.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include "client.hpp"

namespace mag {
namespace xclip {

bool get_clipboard(void*, cz::Allocator allocator, cz::String* text) {
    cz::Str args[] = {"xclip", "-o"};

    cz::Process process;

    cz::Input_File std_out;
    CZ_DEFER(std_out.close());

    {
        cz::Process_Options options;
        CZ_DEFER(options.std_out.close());
        if (!cz::create_process_output_pipe(&options.std_out, &std_out)) {
            return false;
        }

        if (!process.launch_program(args, &options)) {
            return false;
        }
    }

    cz::read_to_string(std_out, allocator, text);

    if (process.join() != 0) {
        return false;
    }

    return true;
}

bool set_clipboard(void*, cz::Str text) {
    cz::Str args[] = {"xclip", "-i"};

    cz::Process process;

    cz::Output_File std_in;
    CZ_DEFER(std_in.close());

    {
        cz::Process_Options options;
        CZ_DEFER(options.std_in.close());
        if (!cz::create_process_input_pipe(&options.std_in, &std_in)) {
            return false;
        }

        // We have to completely write out our payload (the new clipboard's text)
        // before the xclip process starts because otherwise it never reads it.
        int64_t result = cz::write_binary_loop(std_in, text);
        CZ_DEBUG_ASSERT(result == text.len);
        std_in.close();
        std_in = {};

        if (!process.launch_program(args, &options)) {
            return false;
        }
    }

    if (process.join() != 0) {
        return false;
    }

    return true;
}

void use_xclip_clipboard(Client* client) {
    client->set_system_clipboard_func = set_clipboard;
    client->set_system_clipboard_data = nullptr;
    client->get_system_clipboard_func = get_clipboard;
    client->get_system_clipboard_data = nullptr;
}

}
}
