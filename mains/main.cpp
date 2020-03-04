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
        CZ_DEFER(server.drop());
        server.editor.create_buffer("*scratch*");
        server.editor.key_map = create_key_map();
        server.editor.theme = create_theme();

        Client client = server.make_client();
        CZ_DEFER(client.drop());

        if (argc == 2) {
            open_file(&server.editor, &client, argv[1]);
        }

        run_ncurses(&server, &client);
        return 0;
    } catch (cz::PanicReachedException& ex) {
        fprintf(stderr, "Fatal error: %s\n", ex.what());
        return 1;
    }
}
