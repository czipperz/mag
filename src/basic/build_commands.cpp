#include "build_commands.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include "core/command_macros.hpp"
#include "core/job.hpp"
#include "version_control/version_control.hpp"

namespace mag {
namespace basic {

REGISTER_COMMAND(command_build_debug_vc_root);
void command_build_debug_vc_root(Editor* editor, Command_Source source) {
    cz::String top_level_path = {};
    CZ_DEFER(top_level_path.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!version_control::get_root_directory(buffer->directory.buffer, cz::heap_allocator(),
                                                 &top_level_path)) {
            source.client->show_message("No version control repository found");
            return;
        }
    }

#ifdef _WIN32
    cz::Str args[] = {"powershell", ".\\build-debug"};
#else
    cz::Str args[] = {"./build-debug"};
#endif
    run_console_command(source.client, editor, top_level_path.buffer, args, "build debug");
}

}
}
