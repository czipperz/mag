#include "config.hpp"

#include "basic/buffer_commands.hpp"
#include "basic/capitalization_commands.hpp"
#include "basic/commands.hpp"
#include "basic/completion_commands.hpp"
#include "basic/copy_commands.hpp"
#include "basic/cpp_commands.hpp"
#include "basic/directory_commands.hpp"
#include "basic/search_commands.hpp"
#include "basic/shift_commands.hpp"
#include "basic/visible_region_commands.hpp"
#include "basic/window_commands.hpp"
#include "clang_format/clang_format.hpp"
#include "git/git.hpp"
#include "git/tokenize_git_commit_edit_message.hpp"
#include "git/tokenize_patch.hpp"
#include "git/tokenize_rebase_todo.hpp"
#include "man/man.hpp"
#include "overlay.hpp"
#include "prose/alternate.hpp"
#include "syntax/overlay_matching_pairs.hpp"
#include "syntax/overlay_matching_region.hpp"
#include "syntax/overlay_matching_tokens.hpp"
#include "syntax/overlay_preferred_column.hpp"
#include "syntax/tokenize_cpp.hpp"
#include "syntax/tokenize_md.hpp"
#include "syntax/tokenize_path.hpp"
#include "syntax/tokenize_process.hpp"

namespace mag {
namespace custom {

using namespace basic;

#define BIND(MAP, KEYS, FUNC) ((MAP).bind(KEYS, {FUNC, #FUNC}))

Key_Map create_key_map() {
    Key_Map key_map = {};
    BIND(key_map, "C-\\ ", command_set_mark);
    BIND(key_map, "C-@", command_set_mark);
    BIND(key_map, "C-x C-x", command_swap_mark_point);

    BIND(key_map, "C-w", command_cut);
    BIND(key_map, "A-w", command_copy);
    BIND(key_map, "C-y", command_paste);
    BIND(key_map, "A-y", command_paste_previous);

    BIND(key_map, "C-f", command_forward_char);
    BIND(key_map, "C-b", command_backward_char);
    BIND(key_map, "A-f", command_forward_word);
    BIND(key_map, "A-b", command_backward_word);

    BIND(key_map, "C-n", command_forward_line);
    BIND(key_map, "C-p", command_backward_line);
    BIND(key_map, "A-n", command_shift_line_forward);
    BIND(key_map, "A-p", command_shift_line_backward);

    BIND(key_map, "C-A-n", command_create_cursor_forward);
    BIND(key_map, "C-A-p", command_create_cursor_backward);

    BIND(key_map, "C-c c", command_create_cursors_all_search);
    BIND(key_map, "C-c C-A-n", command_create_cursors_to_end_search);
    BIND(key_map, "C-c C-A-p", command_create_cursors_to_start_search);

    BIND(key_map, "C-c /", command_create_cursors_last_change);
    BIND(key_map, "C-c _", command_create_cursors_last_change);
    BIND(key_map, "C-c C-/", command_create_cursors_undo);
    BIND(key_map, "C-c C-_", command_create_cursors_undo);
    BIND(key_map, "C-c A-/", command_create_cursors_redo);
    BIND(key_map, "C-c A-_", command_create_cursors_redo);

    BIND(key_map, "A-<", command_start_of_buffer);
    BIND(key_map, "A->", command_end_of_buffer);

    BIND(key_map, "C-x C-p", command_pop_jump);
    BIND(key_map, "C-x C-n", command_unpop_jump);
    BIND(key_map, "C-x C-\\ ", command_push_jump);
    BIND(key_map, "C-x C-@", command_push_jump);

    BIND(key_map, "C-e", command_end_of_line);
    BIND(key_map, "C-a", command_start_of_line);
    BIND(key_map, "A-a", command_start_of_line_text);

    BIND(key_map, "A-r", command_search_forward);
    BIND(key_map, "C-r", command_search_backward);

    BIND(key_map, "\\-", command_delete_backward_char);
    BIND(key_map, "C-d", command_delete_forward_char);
    BIND(key_map, "A-\\-", command_delete_backward_word);
    BIND(key_map, "A-d", command_delete_forward_word);

    BIND(key_map, "C-k", command_delete_line);
    BIND(key_map, "A-k", command_duplicate_line);
    BIND(key_map, "C-A-k", command_delete_end_of_line);

    BIND(key_map, "C-t", command_transpose_characters);

    BIND(key_map, "A-m", command_open_line);
    BIND(key_map, "C-m", command_insert_newline);

    BIND(key_map, "C-/", command_undo);
    BIND(key_map, "C-_", command_undo);
    BIND(key_map, "A-/", command_redo);
    BIND(key_map, "A-_", command_redo);

    BIND(key_map, "C-g", command_stop_action);

    BIND(key_map, "C-o", command_open_file);
    BIND(key_map, "A-o", command_cycle_window);
    BIND(key_map, "C-s", command_save_file);

    BIND(key_map, "C-x C-c", command_quit);

    BIND(key_map, "C-x 1", command_one_window);
    BIND(key_map, "C-x 2", command_split_window_horizontal);
    BIND(key_map, "C-x 3", command_split_window_vertical);
    BIND(key_map, "C-x 0", command_close_window);

    BIND(key_map, "C-x h", command_mark_buffer);

    BIND(key_map, "C-x b", command_switch_buffer);
    BIND(key_map, "C-x k", command_kill_buffer);

    BIND(key_map, "C-c a", prose::command_alternate);

    BIND(key_map, "C-c u", command_uppercase_letter);
    BIND(key_map, "C-c l", command_lowercase_letter);
    BIND(key_map, "C-c C-u", command_uppercase_region);
    BIND(key_map, "C-c C-l", command_lowercase_region);

    man::path_to_autocomplete_man_page =
        "/home/czipperz/find-man-page/build/release/autocomplete-man-page";
    man::path_to_load_man_page = "/home/czipperz/find-man-page/build/release/load-man-page";
    BIND(key_map, "C-c m", man::command_man);

    BIND(key_map, "A-g A-g", command_goto_line);
    BIND(key_map, "A-g c", command_goto_position);

    BIND(key_map, "A-g n", command_search_open_next);
    BIND(key_map, "A-g p", command_search_open_previous);

    BIND(key_map, "A-g s", git::command_git_grep);

    BIND(key_map, "A-l", command_goto_center_of_window);
    BIND(key_map, "C-l", command_center_in_window);

    BIND(key_map, "A-v", command_up_page);
    BIND(key_map, "C-v", command_down_page);

    return key_map;
}

Theme create_theme() {
    Theme theme = {};
    theme.faces.reserve(cz::heap_allocator(), 34);
    theme.faces.push({-1, -1, 0});              // default
    theme.faces.push({1, -1, 0});               // unsaved buffer
    theme.faces.push({-1, -1, Face::REVERSE});  // selected buffer

    theme.faces.push({0, 7, 0});   // cursor
    theme.faces.push({0, 12, 0});  // marked region

    theme.faces.push({-1, -1, 0});              // minibuffer prompt
    theme.faces.push({-1, -1, Face::REVERSE});  // completion selected item

    theme.faces.push({-1, -1, 0});  // Token_Type::DEFAULT

    theme.faces.push({1, -1, 0});    // Token_Type::KEYWORD
    theme.faces.push({4, -1, 0});    // Token_Type::TYPE
    theme.faces.push({6, -1, 0});    // Token_Type::PUNCTUATION
    theme.faces.push({3, -1, 0});    // Token_Type::OPEN_PAIR
    theme.faces.push({3, -1, 0});    // Token_Type::CLOSE_PAIR
    theme.faces.push({12, -1, 0});   // Token_Type::COMMENT
    theme.faces.push({142, -1, 0});  // Token_Type::DOC_COMMENT
    theme.faces.push({2, -1, 0});    // Token_Type::STRING
    theme.faces.push({-1, -1, 0});   // Token_Type::IDENTIFIER
    theme.faces.push({-1, -1, 0});   // Token_Type::NUMBER

    theme.faces.push({184, -1, 0});  // Token_Type::MERGE_START
    theme.faces.push({184, -1, 0});  // Token_Type::MERGE_MIDDLE
    theme.faces.push({184, -1, 0});  // Token_Type::MERGE_END

    theme.faces.push({3, -1, 0});  // Token_Type::TITLE
    theme.faces.push({2, -1, 0});  // Token_Type::CODE

    theme.faces.push({1, -1, 0});    // Token_Type::PATCH_REMOVE
    theme.faces.push({76, -1, 0});   // Token_Type::PATCH_ADD
    theme.faces.push({246, -1, 0});  // Token_Type::PATCH_NEUTRAL
    theme.faces.push({-1, -1, 0});   // Token_Type::PATCH_ANNOTATION

    theme.faces.push({1, -1, 0});   // Token_Type::GIT_REBASE_TODO_COMMAND
    theme.faces.push({3, -1, 0});   // Token_Type::GIT_REBASE_TODO_SHA
    theme.faces.push({-1, -1, 0});  // Token_Type::GIT_REBASE_TODO_COMMIT_MESSAGE

    theme.faces.push({-1, -1, Face::INVISIBLE});             // Token_Type::PROCESS_ESCAPE_SEQUENCE
    theme.faces.push({-1, -1, Face::BOLD});                  // Token_Type::PROCESS_BOLD
    theme.faces.push({-1, -1, Face::ITALICS});               // Token_Type::PROCESS_ITALICS
    theme.faces.push({-1, -1, Face::BOLD | Face::ITALICS});  // Token_Type::PROCESS_BOLD_ITALICS

    static Overlay overlays[] = {syntax::overlay_matching_region(),
                                 syntax::overlay_preferred_column()};
    theme.overlays = cz::slice(overlays);

    theme.max_completion_results = 5;

    return theme;
}

static Key_Map create_directory_key_map() {
    Key_Map key_map = {};
    BIND(key_map, "C-m", command_directory_open_path);
    BIND(key_map, "\n", command_directory_open_path);
    return key_map;
}

static Key_Map* directory_key_map() {
    static Key_Map key_map = create_directory_key_map();
    return &key_map;
}

static Key_Map create_cpp_key_map() {
    Key_Map key_map = {};
    BIND(key_map, "C-c C-f", clang_format::command_clang_format_buffer);
    BIND(key_map, "A-;", cpp::command_comment);
    return key_map;
}

static Key_Map* cpp_key_map() {
    static Key_Map key_map = create_cpp_key_map();
    return &key_map;
}

static Key_Map create_search_key_map() {
    Key_Map key_map = {};
    BIND(key_map, "C-m", command_search_open);
    BIND(key_map, "\n", command_search_open);
    return key_map;
}

static Key_Map* search_key_map() {
    static Key_Map key_map = create_search_key_map();
    return &key_map;
}

static Key_Map create_path_key_map() {
    Key_Map key_map = {};
    BIND(key_map, "A-i", command_insert_completion);
    BIND(key_map, "C-n", command_next_completion);
    BIND(key_map, "C-p", command_previous_completion);
    BIND(key_map, "C-v", command_completion_down_page);
    BIND(key_map, "A-v", command_completion_up_page);
    BIND(key_map, "A-<", command_first_completion);
    BIND(key_map, "A->", command_last_completion);
    BIND(key_map, "C-l", command_path_up_directory);
    return key_map;
}

static Key_Map* path_key_map() {
    static Key_Map key_map = create_path_key_map();
    return &key_map;
}

static Key_Map create_git_edit_key_map() {
    Key_Map key_map = {};
    BIND(key_map, "C-c C-c", git::command_save_and_quit);
    BIND(key_map, "C-c C-k", git::command_abort_and_quit);
    return key_map;
}

static Key_Map* git_edit_key_map() {
    static Key_Map key_map = create_git_edit_key_map();
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
        static Overlay overlays[] = {syntax::overlay_matching_pairs(),
                                     syntax::overlay_matching_tokens()};
        mode.overlays = cz::slice(overlays);
    } else if (file_name.ends_with(".md")) {
        mode.next_token = syntax::md_next_token;
    } else if (file_name.ends_with(".patch") || file_name.ends_with(".diff")) {
        mode.next_token = syntax::patch_next_token;
        if (file_name.ends_with("/addp-hunk-edit.diff")) {
            mode.key_map = git_edit_key_map();
        }
    } else if (file_name == "*client mini buffer*") {
        mode.next_token = syntax::path_next_token;
        mode.key_map = path_key_map();
    } else if (file_name.starts_with("*git grep ")) {
        mode.next_token = syntax::process_next_token;
        mode.key_map = search_key_map();
    } else if (file_name.starts_with("*man ")) {
        mode.next_token = syntax::process_next_token;
        mode.key_map = search_key_map();
    } else if (file_name.ends_with("/git-rebase-todo")) {
        mode.next_token = syntax::git_rebase_todo_next_token;
        mode.key_map = git_edit_key_map();
    } else if (file_name.ends_with("/COMMIT_EDITMSG")) {
        mode.next_token = syntax::git_commit_edit_message_next_token;
        mode.key_map = git_edit_key_map();
    }
    return mode;
}

}
}
