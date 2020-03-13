#include "window_commands.hpp"

#include "client.hpp"

namespace mag {

void command_one_window(Editor* editor, Command_Source source) {
    Buffer_Id buffer_id = source.client->selected_buffer_id();
    Window::drop(source.client->window);
    source.client->window = Window::create(buffer_id);
    source.client->_selected_window = source.client->window;
}

void command_split_window_horizontal(Editor* editor, Command_Source source) {
    Window* top = Window::create(source.client->selected_buffer_id());
    Window* bottom = Window::create(source.client->selected_buffer_id());

    Window* selected_window = source.client->_selected_window;
    selected_window->tag = Window::HORIZONTAL_SPLIT;

    selected_window->v.horizontal_split.top = top;
    top->parent = selected_window;

    selected_window->v.horizontal_split.bottom = bottom;
    bottom->parent = selected_window;

    source.client->_selected_window = top;
}

void command_split_window_vertical(Editor* editor, Command_Source source) {
    Window* left = Window::create(source.client->selected_buffer_id());
    Window* right = Window::create(source.client->selected_buffer_id());

    Window* selected_window = source.client->_selected_window;
    selected_window->tag = Window::VERTICAL_SPLIT;

    selected_window->v.vertical_split.left = left;
    left->parent = selected_window;

    selected_window->v.vertical_split.right = right;
    right->parent = selected_window;

    source.client->_selected_window = left;
}

static bool is_first(Window* parent, Window* child) {
    switch (parent->tag) {
    case Window::UNIFIED:
        CZ_PANIC("Unified window has children");

    case Window::VERTICAL_SPLIT:
        return parent->v.vertical_split.left == child;

    case Window::HORIZONTAL_SPLIT:
        return parent->v.horizontal_split.top == child;
    }

    CZ_PANIC("");
}

Window* window_first(Window* window) {
    switch (window->tag) {
    case Window::UNIFIED:
        return window;

    case Window::VERTICAL_SPLIT:
        return window_first(window->v.vertical_split.left);

    case Window::HORIZONTAL_SPLIT:
        return window_first(window->v.horizontal_split.top);
    }

    CZ_PANIC("");
}

static Window* second_side(Window* window) {
    switch (window->tag) {
    case Window::UNIFIED:
        CZ_PANIC("Unified window has children");

    case Window::VERTICAL_SPLIT:
        return window->v.vertical_split.right;

    case Window::HORIZONTAL_SPLIT:
        return window->v.horizontal_split.bottom;
    }

    CZ_PANIC("");
}

void command_cycle_window(Editor* editor, Command_Source source) {
    Window* child;
    Window* parent = source.client->_selected_window;
    do {
        child = parent;
        parent = child->parent;
        if (!parent) {
            source.client->_selected_window = window_first(source.client->window);
            return;
        }
    } while (!is_first(parent, child));

    source.client->_selected_window = window_first(second_side(parent));
}

}
