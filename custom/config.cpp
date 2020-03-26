#include "config.hpp"

#include "basic/buffer_commands.hpp"
#include "basic/capitalization_commands.hpp"
#include "basic/commands.hpp"
#include "basic/completion_commands.hpp"
#include "basic/copy_commands.hpp"
#include "basic/directory_commands.hpp"
#include "basic/search_commands.hpp"
#include "basic/shift_commands.hpp"
#include "basic/visible_region_commands.hpp"
#include "basic/window_commands.hpp"
#include "clang_format/clang_format.hpp"
#include "git/git.hpp"
#include "prose/alternate.hpp"
#include "syntax/tokenize_cpp.hpp"
#include "syntax/tokenize_md.hpp"
#include "syntax/tokenize_path.hpp"

namespace mag {
namespace custom {

using namespace basic;

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
    key_map.bind("A-a", command_start_of_line_text);

    key_map.bind("A-r", command_search_forward);
    key_map.bind("C-r", command_search_backward);

    key_map.bind("\\-", command_delete_backward_char);
    key_map.bind("C-d", command_delete_forward_char);
    key_map.bind("A-\\-", command_delete_backward_word);
    key_map.bind("A-d", command_delete_forward_word);

    key_map.bind("C-k", command_delete_line);
    key_map.bind("A-k", command_duplicate_line);
    key_map.bind("C-A-k", command_delete_end_of_line);

    key_map.bind("C-t", command_transpose_characters);

    key_map.bind("A-m", command_open_line);
    key_map.bind("C-m", command_insert_newline);

    key_map.bind("C-/", command_undo);
    key_map.bind("C-_", command_undo);
    key_map.bind("A-/", command_redo);
    key_map.bind("A-_", command_redo);

    key_map.bind("C-g", command_stop_action);

    key_map.bind("C-o", command_open_file);
    key_map.bind("A-o", command_cycle_window);
    key_map.bind("C-s", command_save_file);

    key_map.bind("C-x C-c", command_quit);

    key_map.bind("C-x 1", command_one_window);
    key_map.bind("C-x 2", command_split_window_horizontal);
    key_map.bind("C-x 3", command_split_window_vertical);
    key_map.bind("C-x 0", command_close_window);

    key_map.bind("C-x h", command_mark_buffer);

    key_map.bind("C-x b", command_switch_buffer);
    key_map.bind("C-x k", command_kill_buffer);

    key_map.bind("C-c a", prose::command_alternate);

    key_map.bind("C-c u", command_uppercase_letter);
    key_map.bind("C-c l", command_lowercase_letter);
    key_map.bind("C-c C-u", command_uppercase_region);
    key_map.bind("C-c C-l", command_lowercase_region);

    key_map.bind("A-g A-g", command_goto_line);
    key_map.bind("A-g c", command_goto_position);

    key_map.bind("A-g s", git::command_git_grep);

    key_map.bind("A-l", command_goto_center_of_window);
    key_map.bind("C-l", command_center_in_window);

    return key_map;
}

Theme create_theme() {
    Theme theme = {};
    theme.faces.reserve(cz::heap_allocator(), 11);
    theme.faces.push({7, 0, 0});  // DEFAULT
    theme.faces.push({1, 0, 0});  // KEYWORD
    theme.faces.push({4, 0, 0});  // TYPE
    theme.faces.push({6, 0, 0});  // PUNCTUATION
    theme.faces.push({3, 0, 0});  // OPEN_PAIR
    theme.faces.push({3, 0, 0});  // CLOSE_PAIR
    theme.faces.push({6, 0, 0});  // COMMENT
    theme.faces.push({2, 0, 0});  // STRING
    theme.faces.push({7, 0, 0});  // IDENTIFIER

    theme.faces.push({3, 0, 0});  // TITLE
    theme.faces.push({2, 0, 0});  // CODE
    return theme;
}

static Key_Map create_directory_key_map() {
    Key_Map key_map = {};
    key_map.bind("C-m", command_directory_open_path);
    key_map.bind("\n", command_directory_open_path);
    return key_map;
}

static Key_Map* directory_key_map() {
    static Key_Map key_map = create_directory_key_map();
    return &key_map;
}

static Key_Map create_cpp_key_map() {
    Key_Map key_map = {};
    key_map.bind("C-c C-f", clang_format::command_clang_format_buffer);
    return key_map;
}

static Key_Map* cpp_key_map() {
    static Key_Map key_map = create_cpp_key_map();
    return &key_map;
}

static Key_Map create_search_key_map() {
    Key_Map key_map = {};
    key_map.bind("C-m", command_search_open);
    key_map.bind("\n", command_search_open);
    return key_map;
}

static Key_Map* search_key_map() {
    static Key_Map key_map = create_search_key_map();
    return &key_map;
}

static Key_Map create_path_key_map() {
    Key_Map key_map = {};
    key_map.bind("A-i", command_insert_completion);
    key_map.bind("C-n", command_next_completion);
    key_map.bind("C-p", command_previous_completion);
    key_map.bind("C-l", command_path_up_directory);
    return key_map;
}

static Key_Map* path_key_map() {
    static Key_Map key_map = create_path_key_map();
    return &key_map;
}

Mode get_mode(cz::Str file_name) {
    Mode mode = {};
    mode.next_token = default_next_token;
    if (file_name.ends_with("/")) {
        mode.key_map = directory_key_map();
    } else if (file_name.ends_with(".c") || file_name.ends_with(".h") ||
               file_name.ends_with(".cc") || file_name.ends_with(".hh") ||
               file_name.ends_with(".cpp") || file_name.ends_with(".hpp")) {
        mode.next_token = syntax::cpp_next_token;
        mode.key_map = cpp_key_map();
    } else if (file_name.ends_with(".md")) {
        mode.next_token = syntax::md_next_token;
    } else if (file_name == "*client mini buffer*") {
        mode.next_token = syntax::path_next_token;
        mode.key_map = path_key_map();
    } else if (file_name.starts_with("*git grep ")) {
        mode.key_map = search_key_map();
    }
    return mode;
}

}
}
