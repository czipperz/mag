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

    Editor* editor = &server.editor;
    Command_Source source = {};
    source.client = &client;
    WITH_SELECTED_BUFFER({
        cz::String contents = buffer->contents.stringify(cz::heap_allocator());
        CZ_DEFER(contents.drop(cz::heap_allocator()));
        fwrite(contents.buffer(), 1, contents.len(), stdout);
        putchar('\n');
    });
}
