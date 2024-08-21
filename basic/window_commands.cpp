#include "window_commands.hpp"

#include <cz/util.hpp>
#include "client.hpp"
#include "command_macros.hpp"

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

REGISTER_COMMAND(command_one_window);
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

        Window* sel = first;
        if (!first)
            sel = second;

        if (sel)
            sel->set_size(window->total_rows, window->total_cols);

        Window_Split::drop_non_recursive(window);
        return sel;
    }
    }

    CZ_PANIC("Invalid window tag");
}

REGISTER_COMMAND(command_one_window_except_pinned);
void command_one_window_except_pinned(Editor* editor, Command_Source source) {
    Window* top =
        trickle_up(source.client, source.client->window, source.client->selected_normal_window);
    source.client->window = top;
    source.client->window->parent = nullptr;
}

REGISTER_COMMAND(command_close_window);
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

REGISTER_COMMAND(command_quit_window);
void command_quit_window(Editor* editor, Command_Source source) {
    Window_Unified* child = source.client->selected_normal_window;
    Window_Split* parent = child->parent;
    if (parent) {
        return command_close_window(editor, source);
    }

    source.client->set_selected_buffer(editor->buffers[0]);

    if (source.client->window->tag == Window::UNIFIED) {
        (void)pop_jump(editor, source.client);
    }
}

REGISTER_COMMAND(command_split_window_horizontal);
void command_split_window_horizontal(Editor* editor, Command_Source source) {
    split_window(source.client, Window::HORIZONTAL_SPLIT);
}

REGISTER_COMMAND(command_split_window_vertical);
void command_split_window_vertical(Editor* editor, Command_Source source) {
    split_window(source.client, Window::VERTICAL_SPLIT);
}

REGISTER_COMMAND(command_split_increase_ratio);
void command_split_increase_ratio(Editor* editor, Command_Source source) {
    Window_Split* split = source.client->selected_normal_window->parent;
    if (!split) {
        return;
    }
    split->split_ratio = cz::min(split->split_ratio + 0.1f, 0.9f);
    split->set_children_size();
}

REGISTER_COMMAND(command_split_decrease_ratio);
void command_split_decrease_ratio(Editor* editor, Command_Source source) {
    Window_Split* split = source.client->selected_normal_window->parent;
    if (!split) {
        return;
    }
    split->split_ratio = cz::max(split->split_ratio - 0.1f, 0.1f);
    split->set_children_size();
}

REGISTER_COMMAND(command_split_reset_ratio);
void command_split_reset_ratio(Editor* editor, Command_Source source) {
    Window_Split* split = source.client->selected_normal_window->parent;
    if (!split) {
        return;
    }
    split->split_ratio = 0.5f;
    split->set_children_size();
}

REGISTER_COMMAND(command_cycle_window);
void command_cycle_window(Editor* editor, Command_Source source) {
    cycle_window(source.client);
}

REGISTER_COMMAND(command_reverse_cycle_window);
void command_reverse_cycle_window(Editor* editor, Command_Source source) {
    reverse_cycle_window(source.client);
}

REGISTER_COMMAND(command_swap_windows);
void command_swap_windows(Editor* editor, Command_Source source) {
    Window_Split* split = source.client->selected_normal_window->parent;
    if (!split) {
        return;
    }
    cz::swap(split->first, split->second);
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
            cz::swap(split->first, split->second);
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

        // Rotation pulls selected out.  The next loop will fix for want_first.
        //    a                                  | a
        // ----------------     ->    (selected) |---
        //  b | (selected)                       | b
        //
        // Horizontal:                Vertical:
        //     a                          Horizontal:
        //     Vertical:                      a
        //         b                          b
        //         selected               selected
        if (split->parent) {
            Window_Split* dbl = split->parent;
            cz::swap(dbl->tag, split->tag);
            Window* a = (dbl->first == split ? dbl->second : dbl->first);
            Window* b = (split->first == selected ? split->second : split->first);

            if (dbl->first == split) {
                dbl->second = selected;
                split->first = b;
                split->second = a;
            } else {
                dbl->first = selected;
                split->first = a;
                split->second = b;
            }

            selected->parent = dbl;
            a->parent = split;
            b->parent = split;
        }

        split = split->parent;
        no_rotations = true;
    }
}

REGISTER_COMMAND(command_shift_window_up);
void command_shift_window_up(Editor* editor, Command_Source source) {
    return shift_window(source.client, true, Window::HORIZONTAL_SPLIT);
}
REGISTER_COMMAND(command_shift_window_down);
void command_shift_window_down(Editor* editor, Command_Source source) {
    return shift_window(source.client, false, Window::HORIZONTAL_SPLIT);
}
REGISTER_COMMAND(command_shift_window_left);
void command_shift_window_left(Editor* editor, Command_Source source) {
    return shift_window(source.client, true, Window::VERTICAL_SPLIT);
}
REGISTER_COMMAND(command_shift_window_right);
void command_shift_window_right(Editor* editor, Command_Source source) {
    return shift_window(source.client, false, Window::VERTICAL_SPLIT);
}

}
}
