#include "window.hpp"

#include <stdlib.h>
#include <Tracy.hpp>
#include <cz/heap.hpp>
#include "buffer.hpp"
#include "change.hpp"
#include "client.hpp"
#include "visible_region.hpp"

namespace mag {

Window_Unified* Window_Unified::create(Buffer_Id buffer_id) {
    Window_Unified* window = (Window_Unified*)malloc(sizeof(Window_Unified));
    window->parent = nullptr;
    window->rows = 0;
    window->cols = 0;
    window->tag = Window::UNIFIED;

    window->id = buffer_id;
    window->start_position = 0;

    window->change_index = 0;
    window->cursors = {};
    window->cursors.reserve(cz::heap_allocator(), 1);
    window->cursors.push({});
    window->show_marks = false;
    return window;
}

Window_Unified* Window_Unified::clone() {
    Window_Unified* window = (Window_Unified*)malloc(sizeof(Window_Unified));
    *window = *this;
    window->cursors = this->cursors.clone(cz::heap_allocator());
    return window;
}

void Window_Unified::update_cursors(Buffer* buffer) {
    ZoneScoped;

    cz::Slice<Cursor> cursors = this->cursors;
    cz::Slice<Change> new_changes = {buffer->changes.elems() + change_index,
                                     buffer->changes.len() - change_index};
    for (size_t c = 0; c < cursors.len; ++c) {
        position_after_changes(new_changes, &cursors[c].point);
        position_after_changes(new_changes, &cursors[c].mark);
    }

    bool was_0 = start_position == 0;
    position_after_changes(new_changes, &start_position);
    if (was_0) {
        Contents_Iterator iterator = buffer->contents.start();
        compute_visible_end(this, &iterator);
        if (iterator.position >= cursors[0].point) {
            start_position = 0;
        }
    }

    this->change_index = buffer->changes.len();
}

void Window::drop_(Window* window) {
    switch (window->tag) {
    case UNIFIED: {
        Window_Unified* w = (Window_Unified*)window;
        w->cursors.drop(cz::heap_allocator());
        break;
    }

    case VERTICAL_SPLIT:
    case HORIZONTAL_SPLIT: {
        Window_Split* w = (Window_Split*)window;
        Window::drop_(w->first);
        Window::drop_(w->second);
        break;
    }
    }

    free(window);
}

Window_Split* Window_Split::create(Window::Tag tag, Window* first, Window* second) {
    Window_Split* window = (Window_Split*)malloc(sizeof(Window_Split));
    window->parent = nullptr;
    window->rows = 0;
    window->cols = 0;
    window->tag = tag;

    window->first = first;
    window->second = second;
    return window;
}

void Window_Split::drop_non_recursive(Window_Split* window) {
    free(window);
}

void kill_extra_cursors(Window_Unified* window, Client* client) {
    // :CopyLeak we don't deallocate here
    window->cursors.set_len(1);
    Copy_Chain* copy_chain = window->cursors[0].local_copy_chain;
    if (copy_chain) {
        while (copy_chain->previous) {
            copy_chain = copy_chain->previous;
        }
        copy_chain->previous = client->global_copy_chain;
        client->global_copy_chain = copy_chain;
        window->cursors[0].local_copy_chain = nullptr;
    }
}

}
