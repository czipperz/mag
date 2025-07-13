#include "highlight_commands.hpp"

#include "core/command_macros.hpp"
#include "core/face.hpp"
#include "core/movement.hpp"
#include "core/overlay.hpp"
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

REGISTER_COMMAND(command_toggle_highlight_on_buffer_token_at_point);
void command_toggle_highlight_on_buffer_token_at_point(Editor* editor, Command_Source source) {
    SSOStr query = {};
    CZ_DEFER(query.drop(cz::heap_allocator()));

    WITH_SELECTED_NORMAL_BUFFER(source.client);
    get_token_at_position_contents(buffer, window->cursors[window->selected_cursor].point, &query);

    for (size_t i = 0; i < buffer->mode.overlays.len; ++i) {
        if (syntax::is_overlay_highlight_string(buffer->mode.overlays[i], query.as_str())) {
            buffer->mode.overlays[i].vtable->cleanup(buffer->mode.overlays[i].data);
            buffer->mode.overlays.remove(i);
            return;
        }
    }
    buffer->mode.overlays.reserve(1);
    buffer->mode.overlays.push(syntax::overlay_highlight_string(highlight_face, query.as_str()));
}

}
}
