#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/str.hpp>
#include "client.hpp"
#include "command.hpp"
#include "command_macros.hpp"
#include "file.hpp"
#include "ncurses.hpp"
#include "sdl.hpp"
#include "server.hpp"

using namespace mag;

static int usage(char* program_name) {
    fprintf(stderr,
            "%s [options] [files]\n\
\n\
Mag is a text editor.\n\
Options:\n\
  --help             View the help page.\n\
  --client=CLIENT    Launches a specified client.  If CLIENT=ncurses, a terminal session is launched\n\
                     (Linux only).  If CLIENT=sdl, a window is launched.\n\
",
            program_name);
    return 1;
}

int main(int argc, char** argv) {
    try {
        Server server = {};
        server.editor.create();
        CZ_DEFER(server.drop());
        server.editor.create_buffer("*scratch*");
        server.editor.key_map = custom::create_key_map();
        server.editor.theme = custom::create_theme();

        Client client = server.make_client();
        CZ_DEFER(client.drop());

        int chosen_client = 1;
        for (int i = 1; i < argc; ++i) {
            cz::Str arg = argv[i];
            if (arg == "--help") {
                return usage(argv[0]);
            } else if (arg.starts_with("--client=")) {
                if (arg == "--client=ncurses") {
                    chosen_client = 0;
                } else if (arg == "--client=sdl") {
                    chosen_client = 1;
                } else {
                    return usage(argv[0]);
                }
            } else {
                open_file(&server.editor, &client, arg);
            }
        }

        switch (chosen_client) {
        case 0:
            client::ncurses::run(&server, &client);
            break;
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
