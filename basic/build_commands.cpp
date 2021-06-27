#include "build_commands.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include "command_macros.hpp"
#include "job.hpp"
#include "version_control/version_control.hpp"

namespace mag {
namespace basic {

void command_build_debug_vc_root(Editor* editor, Command_Source source) {
    cz::String top_level_path = {};
    CZ_DEFER(top_level_path.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!version_control::get_root_directory(editor, source.client, buffer->directory.buffer(),
                                                 cz::heap_allocator(), &top_level_path)) {
            return;
        }
    }

#ifdef _WIN32
    cz::Str args[] = {"powershell", ".\\build-debug.ps1"};
#else
    cz::Str args[] = {"./build-debug.sh"};
#endif
    run_console_command(source.client, editor, top_level_path.buffer(), args, "build debug",
                        "Failed to run build-debug.sh");
}

}
}
