#include "jump.hpp"

#include "buffer.hpp"
#include "client.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "visible_region.hpp"
#include "window.hpp"

namespace mag {

void Jump::update(const Buffer* buffer) {
    position_after_changes(
        {buffer->changes.elems() + change_index, buffer->changes.len() - change_index}, &position);
    change_index = buffer->changes.len();
}

void push_jump(Window_Unified* window, Client* client, const Buffer* buffer) {
    kill_extra_cursors(window, client);

    Cursor* cursor = &window->cursors[0];

    // If the previous jump in the jump chain is the exact same then don't push jump.
    Jump* prev = client->jump_chain.pop();
    if (prev) {
        client->jump_chain.unpop();
        if (prev->buffer_handle.ptr_equal(window->buffer_handle)) {
            prev->update(buffer);
            if (prev->position == cursor->point) {
                return;
            }
        }
    }

    Jump jump;
    jump.buffer_handle = window->buffer_handle.clone_downgrade();
    jump.position = cursor->point;
    jump.change_index = buffer->changes.len();
    client->jump_chain.push(jump);
}

bool goto_jump(Editor* editor, Client* client, Jump* jump) {
    // Open the file being jumped to.
    {
        cz::Arc<Buffer_Handle> handle;
        if (!jump->buffer_handle.upgrade(&handle)) {
            return false;
        }
        CZ_DEFER(handle.drop());

        client->set_selected_buffer(handle);
    }

    // Go to the jump point in the file.
    Window_Unified* window = client->selected_window();
    kill_extra_cursors(window, client);
    {
        WITH_CONST_WINDOW_BUFFER(window);
        jump->update(buffer);
    }
    window->show_marks = false;
    window->cursors[0].point = jump->position;
    window->cursors[0].mark = window->cursors[0].point;

    // And center it.
    WITH_WINDOW_BUFFER(window);
    center_in_window(window, buffer->mode, editor->theme,
                     buffer->contents.iterator_at(jump->position));

    return true;
}

}
