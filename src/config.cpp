#include "config.hpp"

#include "commands.hpp"
#include "directory_commands.hpp"
#include "tokenize_cpp.hpp"
#include "window_commands.hpp"

namespace mag {

Key_Map create_key_map() {
    Key_Map key_map = {};
    key_map.bind("C-\\ ", command_set_mark);
    key_map.bind("C-@", command_set_mark);
    key_map.bind("C-x C-x", command_swap_mark_point);

    key_map.bind("C-w", command_cut);
    key_map.bind("A-w", command_copy);
    key_map.bind("C-y", command_paste);

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

    key_map.bind("A-<", command_start_of_buffer);
    key_map.bind("A->", command_end_of_buffer);

    key_map.bind("C-e", command_end_of_line);
    key_map.bind("C-a", command_start_of_line);

    key_map.bind("A-m", command_start_of_line_text);

    key_map.bind("C-s", command_search_forward);
    key_map.bind("C-r", command_search_backward);

    key_map.bind("\\-", command_delete_backward_char);
    key_map.bind("C-d", command_delete_forward_char);
    key_map.bind("A-\\-", command_delete_backward_word);
    key_map.bind("A-d", command_delete_forward_word);

    key_map.bind("C-t", command_transpose_characters);

    key_map.bind("C-o", command_open_line);
    key_map.bind("C-m", command_insert_newline);

    key_map.bind("C-/", command_undo);
    key_map.bind("C-_", command_undo);
    key_map.bind("A-/", command_redo);
    key_map.bind("A-_", command_redo);

    key_map.bind("C-g", command_stop_action);

    key_map.bind("C-x C-f", command_open_file);
    key_map.bind("C-x C-s", command_save_file);

    key_map.bind("C-x C-c", command_quit);

    key_map.bind("C-x 1", command_one_window);
    key_map.bind("C-x 2", command_split_window_vertical);
    key_map.bind("C-x 3", command_split_window_horizontal);
    key_map.bind("C-x o", command_cycle_window);

    return key_map;
}

Theme create_theme() {
    Theme theme = {};
    theme.faces.reserve(cz::heap_allocator(), 9);
    theme.faces.push({7, 0, 0});  // DEFAULT
    theme.faces.push({1, 0, 0});  // KEYWORD
    theme.faces.push({4, 0, 0});  // TYPE
    theme.faces.push({6, 0, 0});  // PUNCTUATION
    theme.faces.push({3, 0, 0});  // OPEN_PAIR
    theme.faces.push({3, 0, 0});  // CLOSE_PAIR
    theme.faces.push({6, 0, 0});  // COMMENT
    theme.faces.push({2, 0, 0});  // STRING
    theme.faces.push({7, 0, 0});  // IDENTIFIER
    return theme;
}

static Key_Map create_directory_key_map() {
    Key_Map key_map = {};
    key_map.bind("C-m", command_directory_open_path);
    key_map.bind("\n", command_directory_open_path);
    return key_map;
}

Key_Map* directory_key_map() {
    static Key_Map key_map = create_directory_key_map();
    return &key_map;
}

Mode get_mode(cz::Str file_name) {
    Mode mode = {};
    if (file_name.ends_with(".c") || file_name.ends_with(".h") || file_name.ends_with(".cc") ||
        file_name.ends_with(".hh") || file_name.ends_with(".cpp") || file_name.ends_with(".hpp")) {
        mode.next_token = cpp_next_token;
    } else {
        mode.next_token = default_next_token;
    }
    return mode;
}

}
