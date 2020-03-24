#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include "client.hpp"
#include "command.hpp"
#include "command_macros.hpp"
#include "file.hpp"
#include "ncurses.hpp"
#include "server.hpp"

using namespace mag;

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

        if (argc == 2) {
            open_file(&server.editor, &client, argv[1]);
        }

        client::ncurses::run(&server, &client);
        return 0;
    } catch (cz::PanicReachedException& ex) {
        fprintf(stderr, "Fatal error: %s\n", ex.what());
        return 1;
    }
}
