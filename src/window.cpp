#include "window.hpp"

#include <stdlib.h>
#include <cz/heap.hpp>

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

}
