#include "config.hpp"

#include "commands.hpp"

namespace mag {

Key_Map create_key_map() {
    Key_Map key_map = {};
    key_map.bind("C-\\ ", command_set_mark);
    key_map.bind("C-@", command_set_mark);
    key_map.bind("C-w", command_delete_region);
    key_map.bind("C-x C-x", command_swap_mark_point);

    key_map.bind("C-f", command_forward_char);
    key_map.bind("C-b", command_backward_char);
    key_map.bind("A-f", command_forward_word);
    key_map.bind("A-b", command_backward_word);

    key_map.bind("C-n", command_forward_line);
    key_map.bind("C-p", command_backward_line);
    key_map.bind("A-n", command_shift_line_forward);
    key_map.bind("A-p", command_shift_line_backward);
    key_map.bind("C-A-n", command_create_cursor_forward);
    key_map.bind("C-A-p", command_create_cursor_backward);

    key_map.bind("C-e", command_end_of_line);
    key_map.bind("C-a", command_start_of_line);

    key_map.bind("C-s", command_search_forward);
    key_map.bind("C-r", command_search_backward);

    key_map.bind("\\-", command_delete_backward_char);
    key_map.bind("C-d", command_delete_forward_char);
    key_map.bind("A-\\-", command_delete_backward_word);
    key_map.bind("A-d", command_delete_forward_word);

    key_map.bind("C-/", command_undo);
    key_map.bind("C-_", command_undo);
    key_map.bind("C-?", command_redo);

    key_map.bind("C-g", command_stop_action);

    key_map.bind("C-x C-c", command_quit);

    key_map.bind("C-x 1", command_one_window);
    key_map.bind("C-x 2", command_split_window_vertical);
    key_map.bind("C-x 3", command_split_window_horizontal);

    return key_map;
}

}
