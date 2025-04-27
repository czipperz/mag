#include "highlight_commands.hpp"

#include "command_macros.hpp"
#include "face.hpp"
#include "overlay.hpp"
#include "overlays/overlay_highlight_string.hpp"

namespace mag {
namespace basic {

extern Face highlight_face;

static void command_add_highlight_to_buffer_callback(Editor* editor,
                                                     Client* client,
                                                     cz::Str query,
                                                     void* _data) {
    WITH_SELECTED_NORMAL_BUFFER(client);
    buffer->mode.overlays.reserve(1);
    buffer->mode.overlays.push(syntax::overlay_highlight_string(highlight_face, query));
}

REGISTER_COMMAND(command_add_highlight_to_buffer);
void command_add_highlight_to_buffer(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Add highlight in buffer: ";
    dialog.response_callback = command_add_highlight_to_buffer_callback;
    source.client->show_dialog(dialog);
}

static void command_remove_highlight_from_buffer_callback(Editor* editor,
                                                          Client* client,
                                                          cz::Str query,
                                                          void* _data) {
    WITH_SELECTED_NORMAL_BUFFER(client);
    for (size_t i = 0; i < buffer->mode.overlays.len; ++i) {
        if (syntax::is_overlay_highlight_string(buffer->mode.overlays[i], query)) {
            buffer->mode.overlays[i].vtable->cleanup(buffer->mode.overlays[i].data);
            buffer->mode.overlays.remove(i);
            return;
        }
    }
    client->show_message("No highlight found");
}

REGISTER_COMMAND(command_remove_highlight_from_buffer);
void command_remove_highlight_from_buffer(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Remove highlight from buffer: ";
    dialog.response_callback = command_remove_highlight_from_buffer_callback;
    source.client->show_dialog(dialog);
}

}
}
