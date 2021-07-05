#include "dialog.hpp"

#include <cz/working_directory.hpp>
#include "command_macros.hpp"
#include "editor.hpp"

namespace mag {

void get_selected_region(Window_Unified* window,
                         const Buffer* buffer,
                         cz::Allocator allocator,
                         cz::String* string) {
    if (!window->show_marks) {
        return;
    }

    uint64_t start = window->cursors[window->selected_cursor].start();
    uint64_t end = window->cursors[window->selected_cursor].end();
    buffer->contents.slice_into(allocator, buffer->contents.iterator_at(start), end, string);
}

void get_selected_region(Editor* editor,
                         Client* client,
                         cz::Allocator allocator,
                         cz::String* string) {
    Window_Unified* window = client->selected_normal_window;
    WITH_CONST_WINDOW_BUFFER(window);
    get_selected_region(window, buffer, allocator, string);
}

void get_selected_window_directory(Editor* editor,
                                   Client* client,
                                   cz::Allocator allocator,
                                   cz::String* string) {
    {
        WITH_CONST_WINDOW_BUFFER(client->selected_normal_window);
        if (buffer->directory.len() > 0) {
            string->reserve(allocator, buffer->directory.len());
            string->append(buffer->directory);
            return;
        }
    }

    if (cz::get_working_directory(allocator, string).is_err()) {
        return;
    }

    string->reserve(allocator, 1);
    string->push('/');
}

}
