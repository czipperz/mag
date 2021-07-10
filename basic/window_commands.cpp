#include "window_commands.hpp"

#include <algorithm>
#include "client.hpp"

namespace mag {
namespace basic {

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

static Window* trickle_up(Client* client, Window* w, Window* selected_window) {
    switch (w->tag) {
    case Window::UNIFIED: {
        Window_Unified* window = (Window_Unified*)w;
        if (window->pinned || w == selected_window) {
            return w;
        } else {
            client->save_offscreen_window(window);
            return nullptr;
        }
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)w;
        Window* first = trickle_up(client, window->first, selected_window);
        Window* second = trickle_up(client, window->second, selected_window);
        if (first && second) {
            window->first = first;
            first->parent = window;
            window->second = second;
            second->parent = window;
            return w;
        }

        Window_Split::drop_non_recursive(window);
        if (first) {
            first->set_size(window->total_rows, window->total_cols);
            return first;
        }
        if (second) {
            second->set_size(window->total_rows, window->total_cols);
            return second;
        }
        return nullptr;
    }
    }

    CZ_PANIC("Invalid window tag");
}

void command_one_window_except_pinned(Editor* editor, Command_Source source) {
    Window* top =
        trickle_up(source.client, source.client->window, source.client->selected_normal_window);
    source.client->window = top;
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

void command_quit_window(Editor* editor, Command_Source source) {
    Window_Unified* child = source.client->selected_normal_window;
    Window_Split* parent = child->parent;
    if (parent) {
        return command_close_window(editor, source);
    }

    source.client->set_selected_buffer(editor->buffers[0]);
}

void command_split_window_horizontal(Editor* editor, Command_Source source) {
    split_window(source.client, Window::HORIZONTAL_SPLIT);
}

void command_split_window_vertical(Editor* editor, Command_Source source) {
    split_window(source.client, Window::VERTICAL_SPLIT);
}

void command_split_increase_ratio(Editor* editor, Command_Source source) {
    Window_Split* split = source.client->selected_normal_window->parent;
    if (!split) {
        return;
    }
    split->split_ratio = cz::min(split->split_ratio + 0.1f, 0.9f);
    split->set_children_size();
}

void command_split_decrease_ratio(Editor* editor, Command_Source source) {
    Window_Split* split = source.client->selected_normal_window->parent;
    if (!split) {
        return;
    }
    split->split_ratio = cz::max(split->split_ratio - 0.1f, 0.1f);
    split->set_children_size();
}

void command_split_reset_ratio(Editor* editor, Command_Source source) {
    Window_Split* split = source.client->selected_normal_window->parent;
    if (!split) {
        return;
    }
    split->split_ratio = 0.5f;
    split->set_children_size();
}

void command_cycle_window(Editor* editor, Command_Source source) {
    cycle_window(source.client);
}

void command_reverse_cycle_window(Editor* editor, Command_Source source) {
    reverse_cycle_window(source.client);
}

void command_swap_windows(Editor* editor, Command_Source source) {
    Window_Split* split = source.client->selected_normal_window->parent;
    if (!split) {
        return;
    }
    std::swap(split->first, split->second);
}

static void shift_window(Client* client, bool want_first, Window::Tag want_tag) {
    Window* selected = client->selected_normal_window;
    Window_Split* split = selected->parent;

    // No other windows so no way to shift.
    if (!split) {
        return;
    }

    bool no_rotations = false;
    while (1) {
        // We want to be on the opposite side so swap and set the orientation.
        if ((selected == split->first) != want_first) {
            std::swap(split->first, split->second);
            split->tag = want_tag;
            return;
        }

        // We are on the right side but the orientation is wrong so fix that.
        if (split->tag != want_tag) {
            split->tag = want_tag;
            return;
        }

        // Nothing else to do.
        if (!split->parent) {
            return;
        }

        // Only allow one rotation per key press.
        if (no_rotations) {
            return;
        }

        // Rotate the selected window up.  Then loop to get it in the right orientation.
        //
        //  a | x         a | c         a |
        // -------  -->  -------  -->  ---| x
        //    c      |      x      |    c |
        //           |             |
        //        rotate         loop
        Window** selected_slot = want_first ? &selected->parent->first : &selected->parent->second;
        if (split->parent->first == split) {
            std::swap(split->parent->second, *selected_slot);
            std::swap(split->parent->second->parent, (*selected_slot)->parent);
        } else {
            std::swap(split->parent->first, *selected_slot);
            std::swap(split->parent->first->parent, (*selected_slot)->parent);
        }

        split = split->parent;
        no_rotations = true;
    }
}

void command_shift_window_up(Editor* editor, Command_Source source) {
    return shift_window(source.client, true, Window::HORIZONTAL_SPLIT);
}
void command_shift_window_down(Editor* editor, Command_Source source) {
    return shift_window(source.client, false, Window::HORIZONTAL_SPLIT);
}
void command_shift_window_left(Editor* editor, Command_Source source) {
    return shift_window(source.client, true, Window::VERTICAL_SPLIT);
}
void command_shift_window_right(Editor* editor, Command_Source source) {
    return shift_window(source.client, false, Window::VERTICAL_SPLIT);
}

}
}
