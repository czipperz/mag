#include "client.hpp"

#include "command_macros.hpp"
#include "editor.hpp"

namespace mag {

Window* Window::alloc() {
    return cz::heap_allocator().alloc<Window>();
}

Window* Window::create(Buffer_Id buffer_id) {
    Window* window = Window::alloc();
    window->parent = nullptr;
    window->tag = Window::UNIFIED;
    window->v.unified_id = buffer_id;
    return window;
}

void Window::drop(Window* window) {
    if (window->tag == VERTICAL_SPLIT) {
        drop(window->v.vertical_split.left);
        drop(window->v.vertical_split.right);
    } else if (window->tag == HORIZONTAL_SPLIT) {
        drop(window->v.horizontal_split.top);
        drop(window->v.horizontal_split.bottom);
    }

    cz::heap_allocator().dealloc({window, sizeof(Window)});
}

void Client::hide_mini_buffer(Editor* editor) {
    restore_selected_buffer();
    dealloc_message();
    void clear_buffer(Editor* editor, Buffer* buffer);
    WITH_BUFFER(mini_buffer, mini_buffer_id(), clear_buffer(editor, mini_buffer));
}

}
