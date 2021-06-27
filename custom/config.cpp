#include "config.hpp"

#include <Tracy.hpp>
#include <cz/defer.hpp>
#include <cz/path.hpp>
#include "basic/buffer_commands.hpp"
#include "basic/build_commands.hpp"
#include "basic/capitalization_commands.hpp"
#include "basic/commands.hpp"
#include "basic/completion_commands.hpp"
#include "basic/copy_commands.hpp"
#include "basic/cpp_commands.hpp"
#include "basic/diff_commands.hpp"
#include "basic/directory_commands.hpp"
#include "basic/help_commands.hpp"
#include "basic/html_commands.hpp"
#include "basic/indent_commands.hpp"
#include "basic/macro_commands.hpp"
#include "basic/markdown_commands.hpp"
#include "basic/number_commands.hpp"
#include "basic/reformat_commands.hpp"
#include "basic/region_movement_commands.hpp"
#include "basic/search_commands.hpp"
#include "basic/shift_commands.hpp"
#include "basic/token_movement_commands.hpp"
#include "basic/visible_region_commands.hpp"
#include "basic/window_commands.hpp"
#include "basic/window_completion_commands.hpp"
#include "clang_format/clang_format.hpp"
#include "decoration.hpp"
#include "git/find_file.hpp"
#include "git/git.hpp"
#include "git/tokenize_git_commit_edit_message.hpp"
#include "git/tokenize_patch.hpp"
#include "git/tokenize_rebase_todo.hpp"
#include "gnu_global/gnu_global.hpp"
#include "man/man.hpp"
#include "overlay.hpp"
#include "prose/alternate.hpp"
#include "prose/search.hpp"
#include "solarized_dark.hpp"
#include "syntax/decoration_column_number.hpp"
#include "syntax/decoration_cursor_count.hpp"
#include "syntax/decoration_line_ending_indicator.hpp"
#include "syntax/decoration_line_number.hpp"
#include "syntax/decoration_pinned_indicator.hpp"
#include "syntax/decoration_read_only_indicator.hpp"
#include "syntax/overlay_highlight_string.hpp"
#include "syntax/overlay_incorrect_indent.hpp"
#include "syntax/overlay_matching_pairs.hpp"
#include "syntax/overlay_matching_region.hpp"
#include "syntax/overlay_matching_tokens.hpp"
#include "syntax/overlay_preferred_column.hpp"
#include "syntax/overlay_trailing_spaces.hpp"
#include "syntax/tokenize_cmake.hpp"
#include "syntax/tokenize_color_test.hpp"
#include "syntax/tokenize_cplusplus.hpp"
#include "syntax/tokenize_css.hpp"
#include "syntax/tokenize_directory.hpp"
#include "syntax/tokenize_general.hpp"
#include "syntax/tokenize_go.hpp"
#include "syntax/tokenize_html.hpp"
#include "syntax/tokenize_javascript.hpp"
#include "syntax/tokenize_key_map.hpp"
#include "syntax/tokenize_markdown.hpp"
#include "syntax/tokenize_path.hpp"
#include "syntax/tokenize_process.hpp"
#include "syntax/tokenize_python.hpp"
#include "syntax/tokenize_search.hpp"
#include "syntax/tokenize_shell_script.hpp"
#include "syntax/tokenize_splash.hpp"

namespace mag {
namespace custom {

using namespace basic;

#define BIND(MAP, KEYS, FUNC) ((MAP).bind(KEYS, {FUNC, #FUNC}))

static void create_key_remap(Key_Remap& key_remap) {
    ZoneScoped;

    // Note: The remap is only checked if the key lookup fails.

    // Terminals rebind all of these keys so we do too
    // so we don't have to double specify these keys.
    key_remap.bind("C-@", "C-SPACE");
    key_remap.bind("C-i", "\t");
    key_remap.bind("C-m", "\n");
    key_remap.bind("C-j", "\n");
    key_remap.bind("C-/", "C-_");
    key_remap.bind("C-h", "BACKSPACE");

    // I hit shift and these keys quite often and want the normal behavior.
    key_remap.bind("S-\n", "\n");
    key_remap.bind("S-BACKSPACE", "BACKSPACE");
}

static void create_key_map(Key_Map& key_map) {
    ZoneScoped;

    BIND(key_map, "F1", command_dump_key_map);
    BIND(key_map, "C-x", command_run_command_by_name);

    BIND(key_map, "C-SPACE", command_set_mark);
    BIND(key_map, "A-x A-x", command_swap_mark_point);

    BIND(key_map, "C-w", command_cut);
    BIND(key_map, "A-w", command_copy);
    BIND(key_map, "A-y", command_paste);
    BIND(key_map, "C-y", command_paste_previous);
    BIND(key_map, "C-INSERT", command_copy);
    BIND(key_map, "S-INSERT", command_paste);

    BIND(key_map, "C-f", command_forward_char);
    BIND(key_map, "C-b", command_backward_char);
    BIND(key_map, "A-f", command_forward_word);
    BIND(key_map, "A-b", command_backward_word);
    BIND(key_map, "C-F", region_movement::command_forward_char);
    BIND(key_map, "C-B", region_movement::command_backward_char);
    BIND(key_map, "A-F", region_movement::command_forward_word);
    BIND(key_map, "A-B", region_movement::command_backward_word);

    BIND(key_map, "A-n", command_forward_line_single_cursor_visual);
    BIND(key_map, "A-p", command_backward_line_single_cursor_visual);
    BIND(key_map, "A-N", region_movement::command_forward_line_single_cursor_visual);
    BIND(key_map, "A-P", region_movement::command_backward_line_single_cursor_visual);
    BIND(key_map, "C-n", command_shift_line_forward);
    BIND(key_map, "C-p", command_shift_line_backward);

    BIND(key_map, "UP", command_backward_line_single_cursor_visual);
    BIND(key_map, "DOWN", command_forward_line_single_cursor_visual);
    BIND(key_map, "LEFT", command_backward_char);
    BIND(key_map, "RIGHT", command_forward_char);
    BIND(key_map, "S-UP", region_movement::command_backward_line_single_cursor_visual);
    BIND(key_map, "S-DOWN", region_movement::command_forward_line_single_cursor_visual);
    BIND(key_map, "S-LEFT", region_movement::command_backward_char);
    BIND(key_map, "S-RIGHT", region_movement::command_forward_char);
    BIND(key_map, "C-LEFT", command_backward_word);
    BIND(key_map, "C-RIGHT", command_forward_word);
    BIND(key_map, "C-S-LEFT", region_movement::command_backward_word);
    BIND(key_map, "C-S-RIGHT", region_movement::command_forward_word);

    BIND(key_map, "A-]", command_forward_paragraph);
    BIND(key_map, "A-[", command_backward_paragraph);
    BIND(key_map, "A-}", region_movement::command_forward_paragraph);
    BIND(key_map, "A-{", region_movement::command_backward_paragraph);

    BIND(key_map, "F9", command_start_recording_macro);
    BIND(key_map, "F10", command_stop_recording_macro);
    BIND(key_map, "F11", command_run_macro);

    BIND(key_map, "C-A-u", command_backward_up_token_pair);
    BIND(key_map, "C-A-d", command_forward_up_token_pair);
    BIND(key_map, "C-A-b", command_backward_token_pair);
    BIND(key_map, "C-A-f", command_forward_token_pair);
    BIND(key_map, "C-A-U", region_movement::command_backward_up_token_pair);
    BIND(key_map, "C-A-D", region_movement::command_forward_up_token_pair);
    BIND(key_map, "C-A-B", region_movement::command_backward_token_pair);
    BIND(key_map, "C-A-F", region_movement::command_forward_token_pair);

    BIND(key_map, "A-j", command_backward_matching_token);
    BIND(key_map, "A-q", command_forward_matching_token);
    BIND(key_map, "C-j", command_create_cursor_backward_matching_token);
    BIND(key_map, "C-q", command_create_cursor_forward_matching_token);
    BIND(key_map, "C-A-j", command_create_cursors_to_start_matching_token);
    BIND(key_map, "C-A-q", command_create_cursors_to_end_matching_token);

    BIND(key_map, "C-A-n", command_create_cursor_forward);
    BIND(key_map, "C-A-p", command_create_cursor_backward);

    BIND(key_map, "A-c A-c", command_create_all_cursors_matching_token_or_search);
    BIND(key_map, "A-c C-A-n", command_create_cursors_to_end_search);
    BIND(key_map, "A-c C-A-p", command_create_cursors_to_start_search);

    BIND(key_map, "A-c /", command_create_cursors_last_change);
    BIND(key_map, "A-c _", command_create_cursors_last_change);
    BIND(key_map, "A-c A-/", command_create_cursors_undo);
    BIND(key_map, "A-c A-_", command_create_cursors_undo);
    BIND(key_map, "A-c C-/", command_create_cursors_redo);
    BIND(key_map, "A-c C-_", command_create_cursors_redo);

    BIND(key_map, "A-c a", command_cursors_align);
    BIND(key_map, "A-c l", command_create_cursors_lines_in_region);
    BIND(key_map, "A-c \n", command_remove_cursors_at_empty_lines);
    BIND(key_map, "A-c BACKSPACE", command_remove_selected_cursor);

    BIND(key_map, "A-c C-w", command_cursors_cut_as_lines);
    BIND(key_map, "A-c A-w", command_cursors_copy_as_lines);
    BIND(key_map, "A-c A-y", command_cursors_paste_as_lines);
    BIND(key_map, "A-c C-y", command_cursors_paste_previous_as_lines);

    BIND(key_map, "A-c #", command_insert_numbers);
    BIND(key_map, "A-c +", command_prompt_increase_numbers);
    BIND(key_map, "A-c -", command_copy_selected_region_length);

    BIND(key_map, "A-<", command_start_of_buffer);
    BIND(key_map, "C-HOME", command_start_of_buffer);
    BIND(key_map, "C-S-HOME", region_movement::command_start_of_buffer);
    BIND(key_map, "A->", command_end_of_buffer);
    BIND(key_map, "C-END", command_end_of_buffer);
    BIND(key_map, "C-S-END", region_movement::command_end_of_buffer);

    BIND(key_map, "A-x A-p", command_pop_jump);
    BIND(key_map, "A-LEFT", command_pop_jump);
    BIND(key_map, "MOUSE4", command_pop_jump);
    BIND(key_map, "A-x A-n", command_unpop_jump);
    BIND(key_map, "A-RIGHT", command_unpop_jump);
    BIND(key_map, "MOUSE5", command_unpop_jump);
    BIND(key_map, "A-x C-SPACE", command_push_jump);

    BIND(key_map, "A-e", command_end_of_line);
    BIND(key_map, "END", command_end_of_line);
    BIND(key_map, "A-E", region_movement::command_end_of_line);
    BIND(key_map, "S-END", region_movement::command_end_of_line);
    BIND(key_map, "A-a", command_start_of_line);
    BIND(key_map, "HOME", command_start_of_line);
    BIND(key_map, "A-A", region_movement::command_start_of_line);
    BIND(key_map, "S-HOME", region_movement::command_start_of_line);
    BIND(key_map, "C-a", command_start_of_line_text);
    BIND(key_map, "C-e", command_end_of_line_text);
    BIND(key_map, "C-A", region_movement::command_start_of_line_text);
    BIND(key_map, "C-E", region_movement::command_end_of_line_text);

    BIND(key_map, "A-r", command_search_forward);
    BIND(key_map, "C-r", command_search_backward);

    BIND(key_map, "BACKSPACE", command_delete_backward_char);
    BIND(key_map, "A-BACKSPACE", command_delete_backward_word);
    BIND(key_map, "C-d", command_delete_forward_char);
    BIND(key_map, "DELETE", command_delete_forward_char);
    BIND(key_map, "S-DELETE", command_delete_forward_char);
    BIND(key_map, "A-DELETE", command_delete_forward_word);
    BIND(key_map, "A-d", command_delete_forward_word);

    BIND(key_map, "A-k", command_delete_line);
    BIND(key_map, "C-k", command_duplicate_line);
    BIND(key_map, "C-A-k", command_delete_end_of_line);

    BIND(key_map, "C-t", command_transpose_characters);

    BIND(key_map, "A-m", command_open_line);
    BIND(key_map, "\n", command_insert_newline_indent);
    BIND(key_map, "\t", command_increase_indent);
    BIND(key_map, "A-i", command_decrease_indent);
    BIND(key_map, "S-\t", command_decrease_indent);
    BIND(key_map, "A-=", command_delete_whitespace);
    BIND(key_map, "A-^", command_merge_lines);

    // Note: consider rebinding this in programming language
    // specific key maps so that the reformat works for comments.
    BIND(key_map, "A-h", command_reformat_paragraph);

    BIND(key_map, "A-/", command_undo);
    BIND(key_map, "A-_", command_undo);
    BIND(key_map, "C-/", command_redo);
    BIND(key_map, "C-_", command_redo);

    BIND(key_map, "C-g", command_stop_action);
    BIND(key_map, "ESCAPE", command_stop_action);

    BIND(key_map, "C-o", command_open_file);
    BIND(key_map, "A-o", command_cycle_window);
    BIND(key_map, "A-O", command_reverse_cycle_window);
    BIND(key_map, "C-s", command_save_file);
    BIND(key_map, "A-s", command_save_file);

    BIND(key_map, "A-x A-c", command_quit);

    BIND(key_map, "A-x 1", command_one_window_except_pinned);
    BIND(key_map, "A-x A-1", command_one_window);
    BIND(key_map, "A-x 2", command_split_window_horizontal);
    BIND(key_map, "A-x 3", command_split_window_vertical);
    BIND(key_map, "A-x 4", command_split_increase_ratio);
    BIND(key_map, "A-x 5", command_split_decrease_ratio);
    BIND(key_map, "A-x A-4", command_split_reset_ratio);
    BIND(key_map, "A-x A-5", command_split_reset_ratio);
    BIND(key_map, "A-x 0", command_close_window);

    BIND(key_map, "A-x h", command_mark_buffer);

    BIND(key_map, "A-x b", command_switch_buffer);
    BIND(key_map, "A-x k", command_kill_buffer);
    BIND(key_map, "A-x r", command_rename_buffer);

    BIND(key_map, "A-x C-d", command_apply_diff);

    BIND(key_map, "A-x A-q", command_configure);
    BIND(key_map, "A-x q", command_toggle_read_only);
    BIND(key_map, "A-x C-q", command_toggle_wrap_long_lines);
    BIND(key_map, "A-x C-A-q", command_toggle_draw_line_numbers);

    BIND(key_map, "A-x v", command_show_date_of_build);

    BIND(key_map, "A-x u", command_uppercase_letter);
    BIND(key_map, "A-x l", command_lowercase_letter);
    BIND(key_map, "A-x A-u", command_uppercase_region_or_word);
    BIND(key_map, "A-x A-l", command_lowercase_region_or_word);

    BIND(key_map, "A-x A-t", command_recapitalize_token_prompt);
    BIND(key_map, "A-x t c", command_recapitalize_token_to_camel);
    BIND(key_map, "A-x t p", command_recapitalize_token_to_pascal);
    BIND(key_map, "A-x t C", command_recapitalize_token_to_pascal);
    BIND(key_map, "A-x t s", command_recapitalize_token_to_snake);
    BIND(key_map, "A-x t _", command_recapitalize_token_to_snake);
    BIND(key_map, "A-x t S", command_recapitalize_token_to_usnake);
    BIND(key_map, "A-x t A-s", command_recapitalize_token_to_ssnake);
    BIND(key_map, "A-x t k", command_recapitalize_token_to_kebab);
    BIND(key_map, "A-x t -", command_recapitalize_token_to_kebab);
    BIND(key_map, "A-x t K", command_recapitalize_token_to_ukebab);
    BIND(key_map, "A-x t A-k", command_recapitalize_token_to_skebab);

    man::path_to_autocomplete_man_page =
        "/home/czipperz/find-man-page/build/release/autocomplete-man-page";
    man::path_to_load_man_page = "/home/czipperz/find-man-page/build/release/load-man-page";
    BIND(key_map, "A-g m", man::command_man);

    BIND(key_map, "A-g A-g", command_goto_line);
    BIND(key_map, "A-g g", command_goto_position);

    BIND(key_map, "A-g n", command_search_continue_next);
    BIND(key_map, "A-g p", command_search_continue_previous);
    BIND(key_map, "A-g A-n", command_search_continue_next);
    BIND(key_map, "A-g A-p", command_search_continue_previous);

    BIND(key_map, "A-g a", prose::command_alternate);

    BIND(key_map, "A-g s", git::command_git_grep);
    BIND(key_map, "A-g A-s", git::command_git_grep_token_at_position);
    BIND(key_map, "A-g f", git::command_git_find_file);
    BIND(key_map, "A-g r", prose::command_search_in_current_directory);
    BIND(key_map, "A-g A-r", prose::command_search_in_current_directory_token_at_position);

    BIND(key_map, "A-g A-t", gnu_global::command_lookup_at_point);
    BIND(key_map, "A-g t", gnu_global::command_lookup_prompt);
    BIND(key_map, "C-c", gnu_global::command_complete_at_point);
    BIND(key_map, "MOUSE3", gnu_global::command_move_mouse_and_lookup_at_point);

    BIND(key_map, "C-l", command_goto_center_of_window);
    BIND(key_map, "A-l", command_center_in_window);

    BIND(key_map, "A-v", command_up_page);
    BIND(key_map, "PAGE_UP", command_up_page);
    BIND(key_map, "C-v", command_down_page);
    BIND(key_map, "PAGE_DOWN", command_down_page);

    BIND(key_map, "SCROLL_DOWN", command_scroll_down);
    BIND(key_map, "SCROLL_UP", command_scroll_up);
    BIND(key_map, "SCROLL_DOWN_ONE", command_scroll_down_one);
    BIND(key_map, "SCROLL_UP_ONE", command_scroll_up_one);
    BIND(key_map, "SCROLL_LEFT", command_scroll_left);
    BIND(key_map, "SCROLL_RIGHT", command_scroll_right);

    BIND(key_map, "C--", command_decrease_font_size);
    // Note that C-_ == C-S--.  Maybe this should be C-+ instead?
    BIND(key_map, "C-_", command_increase_font_size);
}

static void create_theme(Theme& theme) {
    ZoneScoped;

#ifdef _WIN32
    theme.font_file = "C:/Windows/Fonts/MesloLGM-Regular.ttf";
#else
    theme.font_file = "/usr/share/fonts/TTF/MesloLGMDZ-Regular.ttf";
#endif
    theme.font_size = 15;

    theme.colors = mag::theme::solarized_dark;

    theme.special_faces[Face_Type::DEFAULT_MODE_LINE] = {0, 7, 0};
    theme.special_faces[Face_Type::UNSAVED_MODE_LINE] = {160, {}, 0};
    theme.special_faces[Face_Type::SELECTED_MODE_LINE] = {39, 0, Face::REVERSE};

    theme.special_faces[Face_Type::SELECTED_CURSOR] = {0, 39, 0};
    theme.special_faces[Face_Type::SELECTED_REGION] = {0, 159, 0};
    theme.special_faces[Face_Type::OTHER_CURSOR] = {0, 247, 0};
    theme.special_faces[Face_Type::OTHER_REGION] = {0, 255, 0};

    theme.special_faces[Face_Type::MINI_BUFFER_PROMPT] = {{}, {}, 0};
    theme.special_faces[Face_Type::MINI_BUFFER_COMPLETION_SELECTED] = {0, 39, 0};

    theme.special_faces[Face_Type::WINDOW_COMPLETION_NORMAL] = {0, 12, 0};
    theme.special_faces[Face_Type::WINDOW_COMPLETION_SELECTED] = {0, 7, 0};

    theme.special_faces[Face_Type::LINE_NUMBER] = {0, 45, 0};
    theme.special_faces[Face_Type::LINE_NUMBER_RIGHT_PADDING] = {0, {}, 0};
    theme.special_faces[Face_Type::LINE_NUMBER_LEFT_PADDING] = {0, 45, 0};

    theme.special_faces[Face_Type::SEARCH_MODE_RESULT_HIGHLIGHT] = {{}, {}, Face::BOLD};

    theme.token_faces[Token_Type::DEFAULT] = {{}, {}, 0};

    theme.token_faces[Token_Type::KEYWORD] = {1, {}, 0};
    theme.token_faces[Token_Type::TYPE] = {4, {}, 0};
    theme.token_faces[Token_Type::PUNCTUATION] = {6, {}, 0};
    theme.token_faces[Token_Type::OPEN_PAIR] = {3, {}, 0};
    theme.token_faces[Token_Type::CLOSE_PAIR] = {3, {}, 0};
    theme.token_faces[Token_Type::COMMENT] = {12, {}, 0};
    theme.token_faces[Token_Type::DOC_COMMENT] = {142, {}, 0};
    theme.token_faces[Token_Type::STRING] = {2, {}, 0};
    theme.token_faces[Token_Type::IDENTIFIER] = {{}, {}, 0};
    theme.token_faces[Token_Type::NUMBER] = {{}, {}, 0};

    theme.token_faces[Token_Type::MERGE_START] = {184, {}, 0};
    theme.token_faces[Token_Type::MERGE_MIDDLE] = {184, {}, 0};
    theme.token_faces[Token_Type::MERGE_END] = {184, {}, 0};

    theme.token_faces[Token_Type::TITLE] = {3, {}, 0};
    theme.token_faces[Token_Type::CODE] = {2, {}, 0};

    theme.token_faces[Token_Type::PATCH_REMOVE] = {1, {}, 0};
    theme.token_faces[Token_Type::PATCH_ADD] = {76, {}, 0};
    theme.token_faces[Token_Type::PATCH_NEUTRAL] = {246, {}, 0};
    theme.token_faces[Token_Type::PATCH_ANNOTATION] = {{}, {}, 0};

    theme.token_faces[Token_Type::GIT_REBASE_TODO_COMMAND] = {1, {}, 0};
    theme.token_faces[Token_Type::GIT_REBASE_TODO_SHA] = {3, {}, 0};
    theme.token_faces[Token_Type::GIT_REBASE_TODO_COMMIT_MESSAGE] = {{}, {}, 0};

    theme.token_faces[Token_Type::PROCESS_ESCAPE_SEQUENCE] = {{}, {}, Face::INVISIBLE};
    theme.token_faces[Token_Type::PROCESS_BOLD] = {{}, {}, Face::BOLD};
    theme.token_faces[Token_Type::PROCESS_ITALICS] = {{}, {}, Face::ITALICS};
    theme.token_faces[Token_Type::PROCESS_BOLD_ITALICS] = {{}, {}, Face::BOLD | Face::ITALICS};

    theme.token_faces[Token_Type::CSS_PROPERTY] = {51, {}, 0};
    theme.token_faces[Token_Type::CSS_ELEMENT_SELECTOR] = {{}, {}, 0};
    theme.token_faces[Token_Type::CSS_ID_SELECTOR] = {228, {}, 0};
    theme.token_faces[Token_Type::CSS_CLASS_SELECTOR] = {118, {}, 0};
    theme.token_faces[Token_Type::CSS_PSEUDO_SELECTOR] = {208, {}, 0};

    theme.token_faces[Token_Type::HTML_TAG_NAME] = {33, {}, 0};
    theme.token_faces[Token_Type::HTML_ATTRIBUTE_NAME] = {140, {}, 0};
    theme.token_faces[Token_Type::HTML_AMPERSAND_CODE] = {4, {}, 0};

    theme.token_faces[Token_Type::DIRECTORY_COLUMN] = {34, {}, 0};
    theme.token_faces[Token_Type::DIRECTORY_SELECTED_COLUMN] = {46, {}, 0};
    theme.token_faces[Token_Type::DIRECTORY_FILE_TIME] = {{}, {}, 0};
    theme.token_faces[Token_Type::DIRECTORY_FILE_DIRECTORY] = {51, {}, 0};
    theme.token_faces[Token_Type::DIRECTORY_FILE_NAME] = {{}, {}, 0};

    theme.token_faces[Token_Type::SEARCH_COMMAND] = {46, {}, 0};
    theme.token_faces[Token_Type::SEARCH_FILE_NAME] = {214, {}, 0};
    theme.token_faces[Token_Type::SEARCH_FILE_LINE] = {226, {}, 0};
    theme.token_faces[Token_Type::SEARCH_FILE_COLUMN] = {226, {}, 0};
    theme.token_faces[Token_Type::SEARCH_RESULT] = {{}, {}, 0};

    theme.token_faces[Token_Type::SPLASH_LOGO] = {46, {}, 0};
    theme.token_faces[Token_Type::SPLASH_KEY_BIND] = {213, {}, 0};

    theme.decorations.reserve(5);
    theme.decorations.push(syntax::decoration_line_number());
    theme.decorations.push(syntax::decoration_column_number());
    theme.decorations.push(syntax::decoration_cursor_count());
    theme.decorations.push(syntax::decoration_read_only_indicator());
    theme.decorations.push(syntax::decoration_pinned_indicator());

    theme.overlays.reserve(6);
    theme.overlays.push(syntax::overlay_matching_region({{}, 237, 0}));
    theme.overlays.push(syntax::overlay_preferred_column({{}, 21, 0}));
    theme.overlays.push(syntax::overlay_trailing_spaces({{}, 208, 0}));
    theme.overlays.push(syntax::overlay_incorrect_indent({{}, 208, 0}));
    theme.overlays.push(
        syntax::overlay_highlight_string({{}, {}, Face::BOLD}, "TODO", false, Token_Type::COMMENT));
    theme.overlays.push(syntax::overlay_highlight_string({{}, {}, Face::BOLD}, "TODO", false,
                                                         Token_Type::DOC_COMMENT));

    theme.max_completion_results = 5;
    theme.mini_buffer_max_height = 5;
    theme.mini_buffer_completion_filter = spaces_are_wildcards_completion_filter;
    theme.window_completion_filter = prefix_completion_filter;

    theme.mouse_scroll_rows = 4;
    theme.mouse_scroll_cols = 10;

    theme.draw_line_numbers = false;

    theme.allow_animated_scrolling = true;
    theme.scroll_outside_visual_rows = 3;
    theme.scroll_jump_half_page_when_outside_visible_region = false;

    theme.scroll_outside_visual_columns = 10;
}

void editor_created_callback(Editor* editor) {
    create_key_remap(editor->key_remap);
    create_key_map(editor->key_map);
    create_theme(editor->theme);
}

static void directory_key_map(Key_Map& key_map) {
    BIND(key_map, "\n", command_directory_open_path);
    BIND(key_map, "A-j", command_directory_open_path);
    BIND(key_map, "s", command_directory_run_path);
    BIND(key_map, "d", command_directory_delete_path);
    BIND(key_map, "c", command_directory_copy_path);
    BIND(key_map, "r", command_directory_rename_path);
    BIND(key_map, "g", command_directory_reload);
    BIND(key_map, "\t", command_directory_toggle_sort);
    BIND(key_map, "m", command_create_directory);

    BIND(key_map, "n", command_forward_line);
    BIND(key_map, "p", command_backward_line);
}

static void search_key_map(Key_Map& key_map) {
    BIND(key_map, "\n", command_search_open_selected);
    BIND(key_map, "g", command_search_reload);

    BIND(key_map, "o", command_search_open_selected_no_swap);
    BIND(key_map, "n", command_search_open_next_no_swap);
    BIND(key_map, "p", command_search_open_previous_no_swap);
}

static void mini_buffer_key_map(Key_Map& key_map) {
    BIND(key_map, "A-n", command_next_completion);
    BIND(key_map, "DOWN", command_next_completion);
    BIND(key_map, "A-p", command_previous_completion);
    BIND(key_map, "UP", command_previous_completion);
    BIND(key_map, "C-v", command_completion_down_page);
    BIND(key_map, "A-v", command_completion_up_page);
    BIND(key_map, "A-<", command_first_completion);
    BIND(key_map, "A->", command_last_completion);

    BIND(key_map, "A-u", command_path_up_directory);
    BIND(key_map, "C-BACKSPACE", command_path_up_directory);

    BIND(key_map, "A-i", command_insert_completion);
    BIND(key_map, "\t", command_insert_completion);
    BIND(key_map, "A-j", command_insert_completion_and_submit_mini_buffer);
    BIND(key_map, "\n", command_submit_mini_buffer);

    // These keys just mess up the prompt so unbind them.
    BIND(key_map, "C-k", command_do_nothing);
    BIND(key_map, "A-m", command_do_nothing);
}

static void window_completion_key_map(Key_Map& key_map) {
    BIND(key_map, "C-n", window_completion::command_next_completion);
    BIND(key_map, "C-p", window_completion::command_previous_completion);
    BIND(key_map, "C-v", window_completion::command_completion_down_page);
    BIND(key_map, "A-v", window_completion::command_completion_up_page);
    BIND(key_map, "A-<", window_completion::command_first_completion);
    BIND(key_map, "A->", window_completion::command_last_completion);

    BIND(key_map, "\n", window_completion::command_finish_completion);
    BIND(key_map, "C-c", window_completion::command_finish_completion);
}

static void git_edit_key_map(Key_Map& key_map) {
    BIND(key_map, "A-c A-c", git::command_save_and_quit);
    BIND(key_map, "A-c A-k", git::command_abort_and_quit);
}

void buffer_created_callback(Editor* editor, Buffer* buffer) {
    ZoneScoped;

    buffer->mode.indent_width = 4;
    buffer->mode.tab_width = 4;
    buffer->mode.use_tabs = false;

    buffer->mode.indent_after_open_pair = false;

    buffer->mode.search_case_insensitive = true;

    buffer->mode.preferred_column = 100;

    buffer->mode.wrap_long_lines = false;

    window_completion_key_map(buffer->mode.completion_key_map);

    buffer->mode.next_token = default_next_token;

    if (cz::path::has_component(buffer->directory, "mag") ||
        cz::path::has_component(buffer->directory, "cz")) {
        buffer->mode.indent_width = buffer->mode.tab_width = 4;
        buffer->mode.use_tabs = false;
        buffer->mode.preferred_column = 100;
        BIND(buffer->mode.key_map, "A-g c", command_build_debug_git_root);

        if (cz::path::has_component(buffer->directory, "mag/tutorial")) {
            buffer->mode.preferred_column = 80;
        }
    }

    switch (buffer->type) {
    case Buffer::DIRECTORY:
        // When we can't render a time stamp we pad with spaces.
        buffer->mode.use_tabs = false;

        buffer->mode.next_token = syntax::directory_next_token;
        directory_key_map(buffer->mode.key_map);
        break;

    case Buffer::TEMPORARY:
        // Temporary files pretty much never use tabs and also shouldn't
        // have a max column limit.  They're temporary after all!
        buffer->mode.use_tabs = false;
        buffer->mode.preferred_column = -1;

        if (buffer->name == "*client mini buffer*") {
            buffer->mode.next_token = syntax::path_next_token;
            mini_buffer_key_map(buffer->mode.key_map);
        } else if (buffer->name.contains("*git grep ") || buffer->name.contains("*ag ")) {
            buffer->mode.next_token = syntax::search_next_token;
            search_key_map(buffer->mode.key_map);
        } else if (buffer->name.contains("*build ")) {
            // Build will eventually get its own tokenizer and key map.
            buffer->mode.next_token = syntax::search_next_token;
            search_key_map(buffer->mode.key_map);
        } else if (buffer->name.starts_with("*man ")) {
            buffer->mode.next_token = syntax::process_next_token;
        } else if (buffer->name == "*key map*") {
            buffer->mode.next_token = syntax::key_map_next_token;
            BIND(buffer->mode.key_map, "\n", command_go_to_key_map_binding);
        } else if (buffer->name == "*splash page*") {
            buffer->mode.next_token = syntax::splash_next_token;
        }
        break;

    case Buffer::FILE: {
        buffer->mode.decorations.reserve(1);
        buffer->mode.decorations.push(syntax::decoration_line_ending_indicator());

        cz::Str name = buffer->name;

        // Treat `#test.c#` as `test.c`.
        if (name.len > 2 && name.starts_with('#') && name.ends_with('#')) {
            name = name.slice(1, name.len - 1);
        }
        // Treat `test.c~` as `test.c`.
        if (name.ends_with('~')) {
            name = name.slice_end(name.len - 1);
        }

        if (name.ends_with(".c") || name.ends_with(".h") || name.ends_with(".cc") ||
            name.ends_with(".hh") || name.ends_with(".cpp") || name.ends_with(".hpp") ||
            name.ends_with(".cxx") || name.ends_with(".hxx") || name.ends_with(".glsl")) {
            buffer->mode.next_token = syntax::cpp_next_token;
            BIND(buffer->mode.key_map, "A-x A-f", clang_format::command_clang_format_buffer);
            BIND(buffer->mode.key_map, "A-;", cpp::command_comment);
            BIND(buffer->mode.key_map, "A-h", cpp::command_reformat_comment);
            BIND(buffer->mode.key_map, "A-x *", cpp::command_make_indirect);
            BIND(buffer->mode.key_map, "A-x &", cpp::command_make_direct);

            static const Token_Type types[] = {Token_Type::KEYWORD, Token_Type::TYPE,
                                               Token_Type::IDENTIFIER};
            buffer->mode.overlays.reserve(2);
            buffer->mode.overlays.push(syntax::overlay_matching_pairs({-1, 237, 0}));
            buffer->mode.overlays.push(syntax::overlay_matching_tokens({-1, 237, 0}, types));
        } else if (name == "CMakeLists.txt") {
            buffer->mode.next_token = syntax::cmake_next_token;
            BIND(buffer->mode.key_map, "A-h", basic::command_reformat_comment_hash);
            BIND(buffer->mode.key_map, "A-;", basic::command_comment_hash);

            static const Token_Type types[] = {Token_Type::KEYWORD, Token_Type::IDENTIFIER};
            buffer->mode.overlays.reserve(2);
            buffer->mode.overlays.push(syntax::overlay_matching_pairs({-1, 237, 0}));
            buffer->mode.overlays.push(syntax::overlay_matching_tokens({-1, 237, 0}, types));
        } else if (name.ends_with(".md") ||
                   // .rst / ReStructured Text and .txt / Text files aren't
                   // really markdown but they're often pretty similar.
                   name.ends_with(".rst") || name.ends_with(".txt")) {
            buffer->mode.next_token = syntax::md_next_token;
            BIND(buffer->mode.key_map, "A-h", markdown::command_reformat_paragraph);
        } else if (name.ends_with(".css")) {
            buffer->mode.next_token = syntax::css_next_token;
            BIND(buffer->mode.key_map, "A-h", cpp::command_reformat_comment_block_only);

            static const Token_Type types[] = {
                Token_Type::CSS_PROPERTY, Token_Type::CSS_ELEMENT_SELECTOR,
                Token_Type::CSS_ID_SELECTOR, Token_Type::CSS_CLASS_SELECTOR,
                Token_Type::CSS_PSEUDO_SELECTOR};
            buffer->mode.overlays.reserve(2);
            buffer->mode.overlays.push(syntax::overlay_matching_pairs({-1, 237, 0}));
            buffer->mode.overlays.push(syntax::overlay_matching_tokens({-1, 237, 0}, types));
        } else if (name.ends_with(".html")) {
            buffer->mode.next_token = syntax::html_next_token;
            BIND(buffer->mode.key_map, "A-;", html::command_comment);

            static const Token_Type types[] = {Token_Type::HTML_TAG_NAME,
                                               Token_Type::HTML_ATTRIBUTE_NAME};
            buffer->mode.overlays.reserve(2);
            buffer->mode.overlays.push(syntax::overlay_matching_pairs({-1, 237, 0}));
            buffer->mode.overlays.push(syntax::overlay_matching_tokens({-1, 237, 0}, types));
        } else if (name.ends_with(".js")) {
            buffer->mode.next_token = syntax::js_next_token;
            BIND(buffer->mode.key_map, "A-;", cpp::command_comment);
            BIND(buffer->mode.key_map, "A-h", cpp::command_reformat_comment);

            static const Token_Type types[] = {Token_Type::KEYWORD, Token_Type::TYPE,
                                               Token_Type::IDENTIFIER};
            buffer->mode.overlays.reserve(2);
            buffer->mode.overlays.push(syntax::overlay_matching_pairs({-1, 237, 0}));
            buffer->mode.overlays.push(syntax::overlay_matching_tokens({-1, 237, 0}, types));
        } else if (name.ends_with(".go")) {
            buffer->mode.next_token = syntax::go_next_token;
            BIND(buffer->mode.key_map, "A-;", cpp::command_comment);
            BIND(buffer->mode.key_map, "A-h", cpp::command_reformat_comment);

            // Go uses tabs for alignment.
            buffer->mode.tab_width = buffer->mode.indent_width;
            buffer->mode.use_tabs = true;

            static const Token_Type types[] = {Token_Type::KEYWORD, Token_Type::TYPE,
                                               Token_Type::IDENTIFIER};
            buffer->mode.overlays.reserve(2);
            buffer->mode.overlays.push(syntax::overlay_matching_pairs({-1, 237, 0}));
            buffer->mode.overlays.push(syntax::overlay_matching_tokens({-1, 237, 0}, types));
        } else if (name.ends_with(".sh") || name.ends_with(".bash") || name.ends_with(".zsh") ||
                   name == ".bashrc" || name == ".zshrc" || name == "Makefile" ||
                   name == ".gitconfig" ||
                   // Powershell isn't really a shell script but it pretty much works.
                   name.ends_with(".ps1") ||
                   // Perl isn't really a shell script but it pretty much works.
                   name.ends_with(".pl")) {
            if (name == "Makefile" || name == ".gitconfig") {
                // Makefiles must use tabs so set that up automatically.
                buffer->mode.tab_width = buffer->mode.indent_width;
                buffer->mode.use_tabs = true;
            }

            buffer->mode.next_token = syntax::sh_next_token;
            BIND(buffer->mode.key_map, "A-h", basic::command_reformat_comment_hash);
            BIND(buffer->mode.key_map, "A-;", basic::command_comment_hash);

            static const Token_Type types[] = {Token_Type::KEYWORD, Token_Type::IDENTIFIER};
            buffer->mode.overlays.reserve(2);
            buffer->mode.overlays.push(syntax::overlay_matching_pairs({-1, 237, 0}));
            buffer->mode.overlays.push(syntax::overlay_matching_tokens({-1, 237, 0}, types));
        } else if (name.ends_with(".py") || name.ends_with(".gpy")) {
            buffer->mode.next_token = syntax::python_next_token;
            BIND(buffer->mode.key_map, "A-h", basic::command_reformat_comment_hash);
            BIND(buffer->mode.key_map, "A-;", basic::command_comment_hash);

            static const Token_Type types[] = {Token_Type::KEYWORD, Token_Type::IDENTIFIER};
            buffer->mode.overlays.reserve(2);
            buffer->mode.overlays.push(syntax::overlay_matching_pairs({-1, 237, 0}));
            buffer->mode.overlays.push(syntax::overlay_matching_tokens({-1, 237, 0}, types));
        } else if (name.ends_with(".patch") || name.ends_with(".diff")) {
            buffer->mode.next_token = syntax::patch_next_token;
            if (name == "addp-hunk-edit.diff") {
                git_edit_key_map(buffer->mode.key_map);
            }
        } else if (name == "git-rebase-todo") {
            buffer->mode.next_token = syntax::git_rebase_todo_next_token;
            BIND(buffer->mode.key_map, "A-;", basic::command_comment_hash);
            git_edit_key_map(buffer->mode.key_map);
        } else if (name == "COMMIT_EDITMSG") {
            buffer->mode.next_token = syntax::git_commit_edit_message_next_token;
            BIND(buffer->mode.key_map, "A-;", basic::command_comment_hash);
            git_edit_key_map(buffer->mode.key_map);
        } else if (name == "color test") {
            buffer->mode.next_token = syntax::color_test_next_token;
        } else {
            buffer->mode.next_token = syntax::general_next_token;

            static const Token_Type types[] = {Token_Type::KEYWORD, Token_Type::TYPE,
                                               Token_Type::IDENTIFIER};
            buffer->mode.overlays.reserve(2);
            buffer->mode.overlays.push(syntax::overlay_matching_pairs({-1, 237, 0}));
            buffer->mode.overlays.push(syntax::overlay_matching_tokens({-1, 237, 0}, types));
        }
        break;
    }
    }
}

}
}
