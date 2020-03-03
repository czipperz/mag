#include "client.hpp"

#include "command_macros.hpp"
#include "editor.hpp"

namespace mag {

void Client::hide_mini_buffer(Editor* editor) {
    restore_selected_buffer();
    dealloc_message();
    void clear_buffer(Editor* editor, Buffer* buffer);
    WITH_BUFFER(mini_buffer, mini_buffer_id(), clear_buffer(editor, mini_buffer));
}

}
