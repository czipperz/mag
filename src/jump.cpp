#include "jump.hpp"

#include "buffer.hpp"
#include "client.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "visible_region.hpp"
#include "window.hpp"

namespace mag {

void Jump::update(Buffer* buffer) {
    position_after_changes(
        {buffer->changes.elems() + change_index, buffer->changes.len() - change_index}, &position);
    change_index = buffer->changes.len();
}

void push_jump(Window_Unified* window, Client* client, Buffer* buffer) {
    kill_extra_cursors(window, client);

    Cursor* cursor = &window->cursors[0];

    Jump jump;
    jump.buffer_id = window->id;
    jump.position = cursor->point;
    jump.change_index = buffer->changes.len();
    client->jump_chain.push(jump);
}

void goto_jump(Editor* editor, Client* client, Jump* jump) {
    client->set_selected_buffer(jump->buffer_id);

    Window_Unified* window = client->selected_window();
    kill_extra_cursors(window, client);
    {
        WITH_WINDOW_BUFFER(window);
        jump->update(buffer);
    }
    window->show_marks = false;
    window->cursors[0].point = jump->position;
    window->cursors[0].mark = window->cursors[0].point;

    WITH_WINDOW_BUFFER(window);
    center_in_window(window, buffer->contents.iterator_at(jump->position));
}

}
