#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/path.hpp>
#include <cz/str.hpp>
#include "client.hpp"
#include "command.hpp"
#include "command_macros.hpp"
#include "custom/config.hpp"
#include "file.hpp"
#include "ncurses.hpp"
#include "program_info.hpp"
#include "sdl.hpp"
#include "server.hpp"

using namespace mag;

static int usage() {
    fprintf(stderr,
            "%s [options] [files]\n\
\n\
Mag is a text editor.\n\
\n\
Options:\n\
  --help             View the help page.\n\
  --client=CLIENT    Launches a specified client.\n\
\n\
Available clients:\n"
#ifdef HAS_NCURSES
            "  ncurses   in terminal editing\n"
#endif
            "  sdl       grapical window (default)\n",
            program_name);
    return 1;
}

int mag_main(int argc, char** argv) {
    try {
        program_name = argv[0];
        cz::String program_dir_ = cz::Str(program_name).duplicate(cz::heap_allocator());
        CZ_DEFER(program_dir_.drop(cz::heap_allocator()));
        cz::path::convert_to_forward_slashes(program_dir_.buffer(), program_dir_.len());
        auto program_dir_2 = cz::path::directory_component(program_dir_);
        if (program_dir_2.is_present) {
            program_dir_.set_len(program_dir_2.value.len);
            program_dir_.null_terminate();
            program_dir = program_dir_.buffer();
        } else {
            program_dir = ".";
        }

        Server server = {};
        server.editor.create();
        CZ_DEFER(server.drop());

        Buffer scratch = {};
        scratch.type = Buffer::TEMPORARY;
        scratch.name = cz::Str("*scratch*").duplicate(cz::heap_allocator());
        server.editor.create_buffer(scratch);
        server.editor.key_map = custom::create_key_map();
        server.editor.theme = custom::create_theme();

        Client client = server.make_client();
        CZ_DEFER(client.drop());

        int chosen_client = 1;
        for (int i = 1; i < argc; ++i) {
            cz::Str arg = argv[i];
            if (arg == "--help") {
                return usage();
            } else if (arg.starts_with("--client=")) {
#ifdef HAS_NCURSES
                if (arg == "--client=ncurses") {
                    chosen_client = 0;
                    continue;
                }
#endif

                if (arg == "--client=sdl") {
                    chosen_client = 1;
                    continue;
                }

                return usage();
            } else {
                open_file(&server.editor, &client, arg);
            }
        }

        switch (chosen_client) {
#ifdef HAS_NCURSES
        case 0:
            client::ncurses::run(&server, &client);
            break;
#endif

        case 1:
            client::sdl::run(&server, &client);
            break;
        }
        return 0;
    } catch (cz::PanicReachedException& ex) {
        fprintf(stderr, "Fatal error: %s\n", ex.what());
        return 1;
    }
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(_In_ HINSTANCE hInstance,
                   _In_opt_ HINSTANCE hPrevInstance,
                   _In_ LPSTR lpCmdLine,
                   _In_ int nShowCmd) {
    return mag_main(__argc, __argv);
}
#else
int main(int argc, char** argv) {
    return mag_main(argc, argv);
}
#endif
