#include "window_commands.hpp"

#include "client.hpp"

namespace mag {
namespace custom {

static void save_other_windows(Client* client, Window* w, Window* selected_window) {
    switch (w->tag) {
    case Window::UNIFIED: {
        if (w != selected_window) {
            Window_Unified* window = (Window_Unified*)w;
            client->save_offscreen_window(window);
        }
        break;
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)w;
        save_other_windows(client, window->first, selected_window);
        save_other_windows(client, window->second, selected_window);
        Window_Split::drop_non_recursive(window);
        break;
    }
    }
}

void command_one_window(Editor* editor, Command_Source source) {
    save_other_windows(source.client, source.client->window, source.client->selected_normal_window);
    source.client->window = source.client->selected_normal_window;
    source.client->window->parent = nullptr;
}

void command_close_window(Editor* editor, Command_Source source) {
    Window_Unified* child = source.client->selected_normal_window;
    Window_Split* parent = child->parent;
    if (parent) {
        if (parent->first == child) {
            source.client->replace_window(parent, parent->second);
            source.client->selected_normal_window = window_first(parent->second);
        } else {
            CZ_DEBUG_ASSERT(parent->second == child);
            source.client->replace_window(parent, parent->first);
            source.client->selected_normal_window = window_first(parent->first);
        }
        source.client->save_removed_window(child);
        Window_Split::drop_non_recursive(parent);
    }
}

static void split_window(Editor* editor, Command_Source source, Window::Tag tag) {
    Window_Unified* top = source.client->selected_normal_window;
    Window_Unified* bottom = top->clone();

    Window_Split* parent = Window_Split::create(tag, top, bottom);

    source.client->replace_window(top, parent);

    top->parent = parent;
    bottom->parent = parent;

    source.client->selected_normal_window = top;
}

void command_split_window_horizontal(Editor* editor, Command_Source source) {
    split_window(editor, source, Window::HORIZONTAL_SPLIT);
}

void command_split_window_vertical(Editor* editor, Command_Source source) {
    split_window(editor, source, Window::VERTICAL_SPLIT);
}

Window_Unified* window_first(Window* window) {
    switch (window->tag) {
    case Window::UNIFIED:
        return (Window_Unified*)window;

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT:
        return window_first(((Window_Split*)window)->first);
    }

    CZ_PANIC("");
}

void command_cycle_window(Editor* editor, Command_Source source) {
    Window* child = source.client->selected_normal_window;
    Window_Split* parent = child->parent;
    while (parent) {
        if (parent->first == child) {
            source.client->selected_normal_window = window_first(parent->second);
            return;
        }

        child = parent;
        parent = child->parent;
    };

    source.client->selected_normal_window = window_first(source.client->window);
}

}
}
