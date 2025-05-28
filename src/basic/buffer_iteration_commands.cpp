#include "basic/buffer_iteration_commands.hpp"

#include "core/command_macros.hpp"

namespace mag {
namespace basic {

static void perform_iteration(Editor* editor, Client* client, bool select_next) {
    {
        WITH_CONST_SELECTED_BUFFER(client);
        if (buffer->mode.perform_iteration != nullptr) {
            buffer->mode.perform_iteration(editor, client, select_next);
            return;
        }
    }

    toggle_cycle_window(client);
    {
        WITH_CONST_SELECTED_BUFFER(client);
        if (buffer->mode.perform_iteration != nullptr) {
            buffer->mode.perform_iteration(editor, client, select_next);
            return;
        }
    }

    toggle_cycle_window(client);
    client->show_message("Couldn't find iterable buffer");
}

void command_iteration_next(Editor* editor, Command_Source source) {
    perform_iteration(editor, source.client, /*select_next=*/true);
}
void command_iteration_previous(Editor* editor, Command_Source source) {
    perform_iteration(editor, source.client, /*select_next=*/false);
}

}
}
