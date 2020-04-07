#include "jump.hpp"

#include "buffer.hpp"
#include "client.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "window.hpp"

namespace mag {

void Jump::update(Buffer* buffer) {
    position_after_changes(
        {buffer->changes.elems() + change_index, buffer->changes.len() - change_index}, &position);
    change_index = buffer->changes.len();
}

void push_jump(Window_Unified* window, Client* client, Buffer_Id buffer_id, Buffer* buffer) {
    kill_extra_cursors(window, client);

    Cursor* cursor = &window->cursors[0];

    Jump jump;
    jump.buffer_id = buffer_id;
    jump.position = cursor->point;
    jump.change_index = buffer->changes.len();
    client->jump_chain.push(jump);
}

void goto_jump(Editor* editor, Client* client, Jump* jump) {
    client->set_selected_buffer(jump->buffer_id);

    Window_Unified* window = client->selected_window();
    {
        WITH_WINDOW_BUFFER(window);
        jump->update(buffer);
    }
    window->show_marks = false;
    window->cursors[0].point = jump->position;
    window->cursors[0].mark = window->cursors[0].point;
}

}
