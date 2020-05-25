#include "client.hpp"

#include "command_macros.hpp"
#include "editor.hpp"

namespace mag {

cz::Str clear_buffer(Buffer* buffer);

void Client::clear_mini_buffer(Editor* editor) {
    Window_Unified* window = mini_buffer_window();
    WITH_WINDOW_BUFFER(window);
    clear_buffer(buffer);
}

static bool binary_search_offscreen_windows(cz::Slice<Window_Unified*> offscreen_windows,
                                            Buffer_Id id,
                                            size_t* index) {
    size_t start = 0;
    size_t end = offscreen_windows.len;
    while (start < end) {
        size_t mid = (start + end) / 2;
        Window_Unified* w = offscreen_windows[mid];
        CZ_DEBUG_ASSERT(w->tag == Window::UNIFIED);
        if (w->id == id) {
            *index = mid;
            return true;
        } else if (w->id.value < id.value) {
            start = mid + 1;
        } else {
            end = mid;
        }
    }

    *index = start;
    return false;
}

static bool find_matching_window(Window* w, Buffer_Id id, Window_Unified** out) {
    switch (w->tag) {
    case Window::UNIFIED: {
        Window_Unified* window = (Window_Unified*)w;
        if (window->id == id) {
            *out = window;
            return true;
        } else {
            return false;
        }
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)w;
        return find_matching_window(window->first, id, out) ||
               find_matching_window(window->second, id, out);
    }
    }

    CZ_PANIC("");
}

Window_Unified* Client::make_window_for_buffer(Buffer_Id id) {
    Window_Unified* window;

    size_t index;
    if (binary_search_offscreen_windows(_offscreen_windows, id, &index)) {
        window = _offscreen_windows[index];
        _offscreen_windows.remove(index);
        return window;
    }

    if (find_matching_window(this->window, id, &window)) {
        return window->clone();
    } else {
        return Window_Unified::create(id);
    }
}

void Client::save_offscreen_window(Window_Unified* window) {
    size_t index;
    if (binary_search_offscreen_windows(_offscreen_windows, window->id, &index)) {
        // Delete the window because another window is already saved
        Window::drop_(window);
    } else {
        _offscreen_windows.reserve(cz::heap_allocator(), 1);
        _offscreen_windows.insert(index, window);
    }
}

void Client::save_removed_window(Window_Unified* removed_window) {
    Window_Unified* matching_window;
    if (find_matching_window(this->window, removed_window->id, &matching_window)) {
        // Delete the window because another window is currently open
        Window::drop_(removed_window);
    } else {
        // Store the window for later in the offscreen windows
        save_offscreen_window(removed_window);
    }
}

void Client::set_selected_buffer(Buffer_Id id) {
    if (selected_window()->id == id) {
        return;
    }

    Window_Unified* old_selected_window = selected_normal_window;
    selected_normal_window = make_window_for_buffer(id);
    replace_window(old_selected_window, selected_normal_window);
    save_removed_window(old_selected_window);
}

void Client::replace_window(Window* o, Window* n) {
    if (o->parent) {
        CZ_DEBUG_ASSERT(o->parent->tag == Window::VERTICAL_SPLIT ||
                        o->parent->tag == Window::HORIZONTAL_SPLIT);
        if (o->parent->first == o) {
            o->parent->first = n;
            n->parent = o->parent;
        } else {
            CZ_DEBUG_ASSERT(o->parent->second == o);
            o->parent->second = n;
            n->parent = o->parent;
        }
    } else {
        this->window = n;
        n->parent = nullptr;
    }
}

}
