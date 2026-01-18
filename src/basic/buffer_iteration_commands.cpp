#include "basic/buffer_iteration_commands.hpp"

#include "core/command_macros.hpp"
#include "version_control/log.hpp"

namespace mag {
namespace basic {

static void perform_iteration(Editor* editor, Client* client, bool select_next) {
    void (*perform_iteration)(Editor * editor, Client * client, bool select_next);
    {
        WITH_CONST_SELECTED_BUFFER(client);
        perform_iteration = buffer->mode.perform_iteration;
    }
    if (perform_iteration != nullptr) {
        perform_iteration(editor, client, select_next);
        return;
    }

    toggle_cycle_window(client);

    {
        WITH_CONST_SELECTED_BUFFER(client);
        perform_iteration = buffer->mode.perform_iteration;
    }
    if (perform_iteration != nullptr) {
        perform_iteration(editor, client, select_next);
        return;
    }

    toggle_cycle_window(client);
    const char* error =
        version_control::open_diff_buffer_and_lookup_cursor(editor, client, select_next);
    if (error) {
        client->show_message(error);
    }
}

void command_iteration_next(Editor* editor, Command_Source source) {
    perform_iteration(editor, source.client, /*select_next=*/true);
}
void command_iteration_previous(Editor* editor, Command_Source source) {
    perform_iteration(editor, source.client, /*select_next=*/false);
}

}
}
