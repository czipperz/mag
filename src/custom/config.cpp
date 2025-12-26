#include "config.hpp"

#include <cz/defer.hpp>
#include <cz/directory.hpp>
#include <cz/file.hpp>
#include <cz/path.hpp>
#include <cz/sort.hpp>
#include <tracy/Tracy.hpp>
#include "basic/ascii_drawing_commands.hpp"
#include "basic/buffer_commands.hpp"
#include "basic/buffer_iteration_commands.hpp"
#include "basic/build_commands.hpp"
#include "basic/capitalization_commands.hpp"
#include "basic/cmake_commands.hpp"
#include "basic/commands.hpp"
#include "basic/completion_commands.hpp"
#include "basic/configuration_commands.hpp"
#include "basic/copy_commands.hpp"
#include "basic/cpp_commands.hpp"
#include "basic/cursor_commands.hpp"
#include "basic/diff_commands.hpp"
#include "basic/directory_commands.hpp"
#include "basic/hash_commands.hpp"
#include "basic/help_commands.hpp"
#include "basic/highlight_commands.hpp"
#include "basic/html_commands.hpp"
#include "basic/ide_commands.hpp"
#include "basic/indent_commands.hpp"
#include "basic/java_commands.hpp"
#include "basic/javascript_commands.hpp"
#include "basic/macro_commands.hpp"
#include "basic/markdown_commands.hpp"
#include "basic/mouse_commands.hpp"
#include "basic/movement_commands.hpp"
#include "basic/number_commands.hpp"
#include "basic/reformat_commands.hpp"
#include "basic/region_movement_commands.hpp"
#include "basic/remote.hpp"
#include "basic/rust_commands.hpp"
#include "basic/search_buffer_commands.hpp"
#include "basic/search_commands.hpp"
#include "basic/shift_commands.hpp"
#include "basic/table_commands.hpp"
#include "basic/token_movement_commands.hpp"
#include "basic/visible_region_commands.hpp"
#include "basic/window_commands.hpp"
#include "basic/window_completion_commands.hpp"
#include "basic/xclip.hpp"
#include "clang_format/clang_format.hpp"
#include "core/command_macros.hpp"
#include "core/decoration.hpp"
#include "core/file.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/overlay.hpp"
#include "core/program_info.hpp"
#include "decorations/decoration_column_number.hpp"
#include "decorations/decoration_cursor_count.hpp"
#include "decorations/decoration_line_ending_indicator.hpp"
#include "decorations/decoration_line_number.hpp"
#include "decorations/decoration_max_line_number.hpp"
#include "decorations/decoration_pinned_indicator.hpp"
#include "decorations/decoration_read_only_indicator.hpp"
#include "gnu_global/generic.hpp"
#include "man/man.hpp"
#include "overlays/overlay_build_severities.hpp"
#include "overlays/overlay_compiler_messages.hpp"
#include "overlays/overlay_highlight_string.hpp"
#include "overlays/overlay_incorrect_indent.hpp"
#include "overlays/overlay_matching_pairs.hpp"
#include "overlays/overlay_matching_region.hpp"
#include "overlays/overlay_matching_tokens.hpp"
#include "overlays/overlay_merge_conflicts.hpp"
#include "overlays/overlay_nearest_matching_identifier_before_after.hpp"
#include "overlays/overlay_preferred_column.hpp"
#include "overlays/overlay_trailing_spaces.hpp"
#include "prose/alternate.hpp"
#include "prose/find_file.hpp"
#include "prose/open_relpath.hpp"
#include "prose/repository.hpp"
#include "prose/search.hpp"
#include "solarized_dark.hpp"
#include "syntax/tokenize_build.hpp"
#include "syntax/tokenize_cmake.hpp"
#include "syntax/tokenize_color_test.hpp"
#include "syntax/tokenize_cplusplus.hpp"
#include "syntax/tokenize_css.hpp"
#include "syntax/tokenize_ctest.hpp"
#include "syntax/tokenize_directory.hpp"
#include "syntax/tokenize_general.hpp"
#include "syntax/tokenize_general_c_comments.hpp"
#include "syntax/tokenize_general_hash_comments.hpp"
#include "syntax/tokenize_go.hpp"
#include "syntax/tokenize_html.hpp"
#include "syntax/tokenize_javascript.hpp"
#include "syntax/tokenize_key_map.hpp"
#include "syntax/tokenize_markdown.hpp"
#include "syntax/tokenize_mustache.hpp"
#include "syntax/tokenize_path.hpp"
#include "syntax/tokenize_process.hpp"
#include "syntax/tokenize_protobuf.hpp"
#include "syntax/tokenize_python.hpp"
#include "syntax/tokenize_rust.hpp"
#include "syntax/tokenize_search.hpp"
#include "syntax/tokenize_shell_script.hpp"
#include "syntax/tokenize_splash.hpp"
#include "syntax/tokenize_vim_script.hpp"
#include "syntax/tokenize_zig.hpp"
#include "version_control/blame.hpp"
#include "version_control/log.hpp"
#include "version_control/tokenize_diff.hpp"
#include "version_control/tokenize_git_commit_edit_message.hpp"
#include "version_control/tokenize_patch.hpp"
#include "version_control/tokenize_rebase_todo.hpp"
#include "version_control/version_control.hpp"

namespace mag {
namespace prose {

cz::Str alternate_path_1[] = {"/cz/src/"};
cz::Str alternate_path_2[] = {"/cz/include/cz/"};
size_t alternate_path_len = sizeof(alternate_path_1) / sizeof(*alternate_path_1);

cz::Str alternate_extensions_1[] = {".c", ".cc", ".cxx", ".cpp", "-inl.h"};
cz::Str alternate_extensions_2[] = {".h", ".hh", ".hxx", ".hpp", ".h"};
size_t alternate_extensions_len = sizeof(alternate_extensions_1) / sizeof(*alternate_extensions_1);

}

namespace basic {
Face highlight_face = {0, 226, 0};
}

namespace custom {

/// Specify if new file buffers should use carriage returns when they
/// are saved.  Existing file buffers copy the existing format.  This is
/// needed because buffers always use LF (no carriage returns).
///
/// I added the ifdef to make it easy to configure specifically on Windows.
#ifdef _WIN32
bool default_use_carriage_returns = false;
#else
bool default_use_carriage_returns = false;
#endif

CompressionExtensions compression_extensions[] = {
    {[](cz::Str path) { return path.ends_with(".zst"); }, "unzstd"},
    {[](cz::Str path) { return path.ends_with(".gz"); }, "gunzip"},
};
size_t compression_extensions_len =
    sizeof(compression_extensions) / sizeof(*compression_extensions);

/// Controls whether ncurses will configure colors of the terminal.
/// It doesn't look good on my machine so I disabled it.
bool enable_terminal_colors = false;

/// If enabled then Mag will capture mouse events and move the cursor to where the mouse is clicked.
/// I find it more useful to use the mouse to be able to copy text to external programs and thus
/// disable this.  In SDL, copy/paste works with the native buffers so there isn't this problem.
bool enable_terminal_mouse = false;

/// Ncurses clients can't access the system clipboard.  Pasting into Mag via the terminal
/// can't be differentiated between typing characters & iterating through pasted text.
///
/// To compensate for this, we use a heuristic around the number of typed characters in a single
/// graphical frame.  This constant defines the minimum number of characters that must be typed
/// in a single frame before Mag treats the input as a paste.  To disable this behavior, use `0`.
///
/// It is useful to detect pastes in order to:
/// 1. Have a single edit for the insertion.
/// 2. Avoid auto-indent features that could create incorrect amounts of indent.
size_t ncurses_batch_paste_boundary = 6;

using namespace basic;

#define BIND(MAP, KEYS, FUNC) ((MAP).bind(KEYS, COMMAND(FUNC)))

void client_created_callback(Editor* editor, Client* client) {
    if (client->type == Client::NCURSES) {
        // NCurses doesn't have clipboard support so we use xclip.
        xclip::use_xclip_clipboard(client);
    } else {
        // xclip generally works better than the default SDL behavior.
#if 1 && !defined(_WIN32)
        xclip::use_xclip_clipboard(client);
#endif
    }
}

bool find_relpath_in_directory(cz::Str directory, cz::Str path, cz::String* out) {
    return false;
}

bool find_relpath_in_vc(cz::Str vc_dir, cz::Str directory, cz::Str path, cz::String* out) {
    if (path.starts_with("cz/")) {
        cz::Heap_String cz_dir = {};
        CZ_DEFER(cz_dir.drop());
        if (vc_dir.ends_with("/cz")) {
            cz_dir = cz::format(vc_dir, "/include");
            return prose::try_relative_to(cz_dir, path, out);
        } else {
            cz_dir = cz::format(vc_dir, "/cz/include");
            return prose::try_relative_to(cz_dir, path, out);
        }
    }

    if (path.starts_with("tracy/")) {
        cz::Heap_String cz_dir = {};
        CZ_DEFER(cz_dir.drop());
        cz_dir = cz::format(vc_dir, "/tracy/public");
        return prose::try_relative_to(cz_dir, path, out);
    }

    cz::Heap_String src_dir = cz::format(vc_dir, "/src");
    CZ_DEFER(src_dir.drop());
    return prose::try_relative_to(src_dir, path, out);
}

bool find_relpath_globally(cz::Str path, cz::String* out) {
    static char* cpp_subdir = [&]() -> char* {
        cz::Directory_Iterator iterator;
        if (iterator.init("/usr/include/c++") <= 0)
            return nullptr;
        CZ_DEFER(iterator.drop());
        return cz::format("/usr/include/c++/", iterator.str_name()).buffer;
    }();
    static char* gnu_subdir = [&]() -> char* {
        cz::Directory_Iterator iterator;
        if (iterator.init("/usr/include/x86_64-linux-gnu/c++") <= 0)
            return nullptr;
        CZ_DEFER(iterator.drop());
        return cz::format("/usr/include/x86_64-linux-gnu/c++/", iterator.str_name()).buffer;
    }();
    return prose::try_relative_to("/usr/include", path, out) ||
           prose::try_relative_to("/usr/include/x86_64-linux-gnu", path, out) ||
           (cpp_subdir && prose::try_relative_to(cpp_subdir, path, out)) ||
           (gnu_subdir && prose::try_relative_to(gnu_subdir, path, out));
}

bool find_tags(cz::Str directory, tags::Engine* engine, cz::String* found_directory) {
    // Demo code to jump to $vc_root/src and search for tags from there.
    if (0) {
        cz::String src_dir = {};
        CZ_DEFER(src_dir.drop(cz::heap_allocator()));
        if (!version_control::get_root_directory(directory, cz::heap_allocator(), &src_dir)) {
            return false;
        }
        src_dir.reserve_exact(cz::heap_allocator(), 5);
        src_dir.append("/src");
        src_dir.null_terminate();
        return tags::try_directory(src_dir, engine, found_directory);
    }

    return false;
}

void console_command_finished_callback(Editor* editor, Client* client,
                                       const cz::Arc<Buffer_Handle>& buffer_handle) {
    WITH_CONST_BUFFER_HANDLE(buffer_handle);
    if (buffer->mode.next_token == syntax::build_next_token) {
        prose::install_messages(buffer, buffer_handle);
    }
}

static void create_key_remap(Key_Remap& key_remap) {
    ZoneScoped;

    // Note: The remap is only checked if the key lookup fails.

    // Terminals rebind all of these keys so we do too
    // so we don't have to double specify these keys.
    key_remap.bind("C-@", "C-SPACE");
    key_remap.bind("C-i", "TAB");
    key_remap.bind("C-m", "ENTER");
    key_remap.bind("C-j", "ENTER");
    key_remap.bind("C-/", "C-_");
    key_remap.bind("C-h", "BACKSPACE");
    key_remap.bind("C-A-h", "C-A-BACKSPACE");

    // I hit shift and these keys quite often and want the normal behavior.
    key_remap.bind("S-ENTER", "ENTER");
    key_remap.bind("S-BACKSPACE", "BACKSPACE");
}

static void create_key_map(Key_Map& key_map) {
    ZoneScoped;

    BIND(key_map, "F1", command_dump_key_map);
    BIND(key_map, "C-x", command_run_command_by_name);
    BIND(key_map, "F2", tags::command_lookup_previous_command);
    BIND(key_map, "F3", version_control::command_git_diff_master);
    BIND(key_map, "F5", command_reload_buffer);

    BIND(key_map, "C-SPACE", command_set_mark);
    BIND(key_map, "A-x A-x", command_swap_mark_point);
    BIND(key_map, "A-x A-SPACE", command_show_marks);

    BIND(key_map, "C-w", command_cut);
    BIND(key_map, "A-w", command_copy);
    BIND(key_map, "A-y", command_paste);
    BIND(key_map, "C-y", command_paste_previous);
    BIND(key_map, "C-INSERT", command_copy);
    BIND(key_map, "S-INSERT", command_paste);
    BIND(key_map, "C-X", command_cut);
    BIND(key_map, "C-C", command_copy);
    BIND(key_map, "C-V", command_paste);

#ifdef __APPLE__
    BIND(key_map, "G-x", command_cut);
    BIND(key_map, "G-c", command_copy);
    BIND(key_map, "G-v", command_paste);
#endif

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

    BIND(key_map, "C-A-UP", command_shift_window_up);
    BIND(key_map, "C-A-DOWN", command_shift_window_down);
    BIND(key_map, "C-A-LEFT", command_shift_window_left);
    BIND(key_map, "C-A-RIGHT", command_shift_window_right);

    BIND(key_map, "C-(", command_insert_pair);
    BIND(key_map, "C-[", command_insert_pair);
    BIND(key_map, "C-{", command_insert_pair);

    BIND(key_map, "A-]", command_forward_paragraph);
    BIND(key_map, "A-[", command_backward_paragraph);
    BIND(key_map, "A-}", region_movement::command_forward_paragraph);
    BIND(key_map, "A-{", region_movement::command_backward_paragraph);

    BIND(key_map, "F9", command_start_recording_macro);
    BIND(key_map, "F10", command_stop_recording_macro);
    BIND(key_map, "F11", command_run_macro);
    BIND(key_map, "F12", command_run_macro_forall_lines_in_search);

    BIND(key_map, "C-A-a", command_backward_up_token_pair);
    BIND(key_map, "C-A-e", command_forward_up_token_pair);
    BIND(key_map, "C-A-b", command_backward_token_pair);
    BIND(key_map, "C-A-f", command_forward_token_pair);
    BIND(key_map, "C-A-A", region_movement::command_backward_up_token_pair);
    BIND(key_map, "C-A-E", region_movement::command_forward_up_token_pair);
    BIND(key_map, "C-A-B", region_movement::command_backward_token_pair);
    BIND(key_map, "C-A-F", region_movement::command_forward_token_pair);

    BIND(key_map, "A-u", command_backward_matching_token);
    BIND(key_map, "A-q", command_forward_matching_token);
    BIND(key_map, "C-u", command_create_cursor_backward_matching_token);
    BIND(key_map, "C-q", command_create_cursor_forward_matching_token);
    BIND(key_map, "C-A-u", command_create_cursors_to_start_matching_token);
    BIND(key_map, "C-A-q", command_create_cursors_to_end_matching_token);

    BIND(key_map, "C-A-n", command_create_cursor_forward);
    BIND(key_map, "C-A-p", command_create_cursor_backward);

    BIND(key_map, "A-c A-c", command_create_all_cursors_matching_token_or_search);
    BIND(key_map, "A-c C-A-n", command_create_cursors_to_end_search);
    BIND(key_map, "A-c C-A-p", command_create_cursors_to_start_search);

    BIND(key_map, "A-c f", command_filter_cursors_looking_at);
    BIND(key_map, "A-c A-f", command_filter_cursors_not_looking_at);

    BIND(key_map, "A-c /", command_create_cursors_last_change);
    BIND(key_map, "A-c _", command_create_cursors_last_change);
    BIND(key_map, "A-c A-/", command_create_cursors_undo_nono);
    BIND(key_map, "A-c A-_", command_create_cursors_undo_nono);
    BIND(key_map, "A-c C-/", command_create_cursors_redo_nono);
    BIND(key_map, "A-c C-_", command_create_cursors_redo_nono);

    BIND(key_map, "A-c a", command_cursors_align);
    BIND(key_map, "A-c 0", command_cursors_align_leftpad0);
    BIND(key_map, "A-c l", command_create_cursors_lines_in_region);
    BIND(key_map, "A-c ENTER", command_remove_cursors_at_empty_lines);
    BIND(key_map, "A-c BACKSPACE", command_remove_selected_cursor);

    BIND(key_map, "A-c C-w", command_cursors_cut_as_lines);
    BIND(key_map, "A-c A-w", command_cursors_copy_as_lines);
    BIND(key_map, "A-c A-y", command_cursors_paste_as_lines);
    BIND(key_map, "A-c C-y", command_cursors_paste_previous_as_lines);

    BIND(key_map, "A-c <", command_sort_lines_ascending);
    BIND(key_map, "A-c >", command_sort_lines_descending);
    BIND(key_map, "A-c A-<", command_sort_lines_ascending_shortlex);
    BIND(key_map, "A-c A->", command_sort_lines_descending_shortlex);
    BIND(key_map, "A-c |", command_flip_lines);
    BIND(key_map, "A-c D", command_deduplicate_lines);

    BIND(key_map, "A-c @", command_insert_letters);
    BIND(key_map, "A-c #", command_insert_numbers);
    BIND(key_map, "A-c +", command_prompt_increase_numbers);
    BIND(key_map, "A-c *", command_prompt_multiply_numbers);
    BIND(key_map, "A-c -", command_copy_selected_region_length);

    BIND(key_map, "A-<", command_start_of_buffer);
    BIND(key_map, "C-HOME", command_start_of_buffer);
    BIND(key_map, "C-S-HOME", region_movement::command_start_of_buffer);
    BIND(key_map, "A->", command_end_of_buffer);
    BIND(key_map, "C-END", command_end_of_buffer);
    BIND(key_map, "C-S-END", region_movement::command_end_of_buffer);

    BIND(key_map, "MOUSE1", command_mouse_select_start);
    BIND(key_map, "S-MOUSE1", command_mouse_select_continue);
    BIND(key_map, "MOUSE2", command_copy_paste);

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
    BIND(key_map, "A-R", command_search_forward_expanding);
    BIND(key_map, "C-R", command_search_backward_expanding);

    BIND(key_map, "A-c A-r", command_search_forward_identifier);
    BIND(key_map, "A-c C-r", command_search_backward_identifier);

    BIND(key_map, "INSERT", command_toggle_insert_replace);

    BIND(key_map, "BACKSPACE", command_delete_backward_char);
    BIND(key_map, "A-BACKSPACE", command_delete_backward_word);
    BIND(key_map, "C-BACKSPACE", command_delete_backward_word);
    BIND(key_map, "C-A-BACKSPACE", command_delete_start_of_line_text);
    BIND(key_map, "C-d", command_delete_forward_char);
    BIND(key_map, "DELETE", command_delete_forward_char);
    BIND(key_map, "S-DELETE", command_delete_forward_char);
    BIND(key_map, "A-DELETE", command_delete_forward_word);
    BIND(key_map, "C-DELETE", command_delete_forward_word);
    BIND(key_map, "A-d", command_delete_forward_word);

    BIND(key_map, "C-=", command_fill_region_with_spaces);
    BIND(key_map, "C-A-=", command_fill_region_or_solt_with_spaces);

    BIND(key_map, "A-k", command_delete_line);
    BIND(key_map, "C-k", command_duplicate_line);
    BIND(key_map, "C-A-k", command_delete_end_of_line);
    BIND(key_map, "A-c k", command_duplicate_line_prompt);

    BIND(key_map, "C-t", command_transpose_characters);
    BIND(key_map, "A-t", command_transpose_words);
    BIND(key_map, "C-A-t", command_transpose_tokens);

    BIND(key_map, "C-A-y", command_duplicate_token);
    BIND(key_map, "C-A-w", command_delete_forward_token);

    BIND(key_map, "A-m", command_open_line);
    BIND(key_map, "ENTER", command_insert_newline_indent);
    BIND(key_map, "A-ENTER", command_insert_newline_copy_indent_and_modifiers);
    BIND(key_map, "TAB", command_increase_indent);
    BIND(key_map, "A-i", command_decrease_indent);
    BIND(key_map, "S-TAB", command_decrease_indent);
    BIND(key_map, "A-=", command_delete_whitespace);
    BIND(key_map, "A-SPACE", command_one_whitespace);
    BIND(key_map, "A-^", command_merge_lines);

    // Note: consider rebinding this in programming language
    // specific key maps so that the reformat works for comments.
    BIND(key_map, "A-h", command_reformat_paragraph);
    sentences_start_with_two_spaces = true;

    BIND(key_map, "A-/", command_undo);
    BIND(key_map, "A-_", command_undo);
    BIND(key_map, "C-/", command_redo);
    BIND(key_map, "C-_", command_redo);

    BIND(key_map, "C-g", command_stop_action);
    BIND(key_map, "ESCAPE", command_stop_action);

    BIND(key_map, "C-o", command_open_file);
    BIND(key_map, "C-O", command_open_file_full_path);
    BIND(key_map, "A-o", command_cycle_window);
    BIND(key_map, "A-O", command_reverse_cycle_window);
    BIND(key_map, "C-s", command_save_file);
    BIND(key_map, "A-s", command_save_file);

    BIND(key_map, "A-x A-c", command_quit);

    BIND(key_map, "A-1", command_one_window_except_pinned);
    BIND(key_map, "C-A-1", command_one_window);
    BIND(key_map, "A-2", command_split_window_horizontal);
    BIND(key_map, "A-3", command_split_window_vertical);
    BIND(key_map, "A-4", command_split_decrease_ratio);
    BIND(key_map, "A-5", command_split_increase_ratio);
    BIND(key_map, "C-A-4", command_split_reset_ratio);
    BIND(key_map, "C-A-5", command_split_reset_ratio);
    BIND(key_map, "A-0", command_close_window);

    BIND(key_map, "A-x h", command_mark_buffer);

    BIND(key_map, "A-x b", command_switch_buffer);
    BIND(key_map, "A-x k", command_kill_buffer);
    BIND(key_map, "A-x r", command_rename_buffer);
    BIND(key_map, "A-x w", command_save_buffer_to);
    BIND(key_map, "A-x A-r", command_pretend_rename_buffer);
    BIND(key_map, "A-x d", command_delete_file_and_kill_buffer);

    BIND(key_map, "A-x A-d", command_apply_diff);

    BIND(key_map, "A-x A-q", command_configure);
    BIND(key_map, "A-x q", command_toggle_read_only);
    BIND(key_map, "A-x C-q", command_toggle_wrap_long_lines);
    BIND(key_map, "A-x C-A-q", command_toggle_draw_line_numbers);

    BIND(key_map, "A-x v", command_show_date_of_build);

    BIND(key_map, "A-x u", command_uppercase_letter);
    BIND(key_map, "A-x l", command_lowercase_letter);
    BIND(key_map, "A-x A-u", command_uppercase_region_or_word);
    BIND(key_map, "A-x A-l", command_lowercase_region_or_word);

    BIND(key_map, "A-!", command_run_command_for_result);
    BIND(key_map, "A-@", command_run_command_ignore_result);
    BIND(key_map, "A-#", command_run_command_for_result_in_vc_root);
    BIND(key_map, "A-x C-t", command_launch_terminal);

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

    BIND(key_map, "A-x m", man::command_man);
    BIND(key_map, "A-x A-m", man::command_man_at_point);

    BIND(key_map, "A-g A-g", command_goto_line);
    BIND(key_map, "A-g G", command_goto_position);
    BIND(key_map, "A-g g", command_show_file_length_info);
    BIND(key_map, "A-g c", command_goto_column);

    BIND(key_map, "A-g n", command_iteration_next);
    BIND(key_map, "A-g p", command_iteration_previous);
    BIND(key_map, "A-g A-n", command_iteration_next);
    BIND(key_map, "A-g A-p", command_iteration_previous);

    BIND(key_map, "A-g a", prose::command_alternate);
    BIND(key_map, "A-g b", prose::command_alternate_and_rfind_token_at_cursor);
    BIND(key_map, "A-g A-b", command_toggle_highlight_on_buffer_token_at_point);

    BIND(key_map, "A-g s", prose::command_search_in_version_control_prompt);
    BIND(key_map, "A-g A-s", prose::command_search_in_version_control_token_at_position);
    BIND(key_map, "A-g C-s", prose::command_search_in_version_control_word_prompt);
    BIND(key_map, "A-g r", prose::command_search_in_current_directory_prompt);
    BIND(key_map, "A-g A-r", prose::command_search_in_current_directory_token_at_position);
    BIND(key_map, "A-g C-r", prose::command_search_in_current_directory_word_prompt);
    BIND(key_map, "A-g e", prose::command_search_in_file_prompt);
    BIND(key_map, "A-g A-e", prose::command_search_in_file_token_at_position);
    BIND(key_map, "A-g C-e", prose::command_search_in_file_word_prompt);

    BIND(key_map, "A-g f", prose::command_find_file_in_version_control);
    BIND(key_map, "A-g h", prose::command_find_file_in_current_directory);
    BIND(key_map, "A-g d", prose::command_find_file_diff_master);

    BIND(key_map, "A-g u", prose::command_open_file_on_repo_site);

    BIND(key_map, "A-g A-t", tags::command_lookup_at_point);
    BIND(key_map, "A-g t", tags::command_lookup_prompt);
    BIND(key_map, "A-g A-c", tags::command_complete_at_point);
    BIND(key_map, "MOUSE3", tags::command_move_mouse_and_lookup_at_point);

    BIND(key_map, "C-c", command_complete_at_point_nearest_matching_before_after);
    BIND(key_map, "C-A-c", command_complete_at_point_prompt_identifiers);
    BIND(key_map, "A-,", command_copy_rest_of_line_from_nearest_matching_identifier);

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
    BIND(key_map, "C-+", command_increase_font_size);

    BIND(key_map, "MENU", command_do_nothing);
    BIND(key_map, "SCROLL_LOCK", command_do_nothing);
}

static void create_theme(Theme& theme) {
    ZoneScoped;

#ifdef _WIN32
    theme.font_file = "C:/Windows/Fonts/MesloLGM-Regular.ttf";
#else
    theme.font_file = "/usr/share/fonts/TTF/MesloLGMDZ-Regular.ttf";
#endif
    theme.font_size = 13;

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
    theme.token_faces[Token_Type::DIVIDER_PAIR] = {3, {}, 0};
    theme.token_faces[Token_Type::CLOSE_PAIR] = {3, {}, 0};
    theme.token_faces[Token_Type::COMMENT] = {12, {}, 0};
    theme.token_faces[Token_Type::DOC_COMMENT] = {142, {}, 0};
    theme.token_faces[Token_Type::STRING] = {2, {}, 0};
    theme.token_faces[Token_Type::IDENTIFIER] = {{}, {}, 0};
    theme.token_faces[Token_Type::NUMBER] = {{}, {}, 0};

    theme.token_faces[Token_Type::PREPROCESSOR_KEYWORD] = {207, {}, 0};
    theme.token_faces[Token_Type::PREPROCESSOR_IF] = {219, {}, 0};
    theme.token_faces[Token_Type::PREPROCESSOR_ELSE] = {219, {}, 0};
    theme.token_faces[Token_Type::PREPROCESSOR_ENDIF] = {219, {}, 0};

    theme.token_faces[Token_Type::MERGE_START] = {184, {}, 0};
    theme.token_faces[Token_Type::MERGE_MIDDLE] = {184, {}, 0};
    theme.token_faces[Token_Type::MERGE_END] = {184, {}, 0};

    theme.token_faces[Token_Type::TITLE] = {3, {}, 0};
    theme.token_faces[Token_Type::CODE] = {2, {}, 0};
    theme.token_faces[Token_Type::LINK_TITLE] = {{}, {}, Face::BOLD};
    theme.token_faces[Token_Type::LINK_HREF] = {{}, {}, Face::UNDERSCORE};

    theme.token_faces[Token_Type::PATCH_COMMIT_CONTEXT] = {{}, {}, Face::REVERSE};
    theme.token_faces[Token_Type::PATCH_FILE_CONTEXT] = {7, 129, 0};
    theme.token_faces[Token_Type::PATCH_REMOVE] = {1, {}, 0};
    theme.token_faces[Token_Type::PATCH_ADD] = {76, {}, 0};
    theme.token_faces[Token_Type::PATCH_NEUTRAL] = {246, {}, 0};
    theme.token_faces[Token_Type::PATCH_ANNOTATION] = {45, {}, 0};

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

    theme.token_faces[Token_Type::BLAME_HASH] = {{}, {}, Face::UNDERSCORE};
    theme.token_faces[Token_Type::BLAME_COMMITTER] = {196, {}, 0};
    theme.token_faces[Token_Type::BLAME_DATE] = {46, {}, 0};
    theme.token_faces[Token_Type::BLAME_CONTENTS] = {{}, {}, 0};

    theme.token_faces[Token_Type::BUILD_LOG_FILE_HEADER] = {{}, {}, Face::REVERSE};
    theme.token_faces[Token_Type::BUILD_LOG_LINK] = {{}, {}, Face::UNDERSCORE};

    theme.token_faces[Token_Type::TEST_LOG_FILE_HEADER] = {{}, {}, Face::REVERSE};
    theme.token_faces[Token_Type::TEST_LOG_TEST_CASE_HEADER] = {7, 129, 0};
    theme.token_faces[Token_Type::TEST_LOG_LINK] = {{}, {}, Face::UNDERSCORE};

    theme.token_faces[Token_Type::BUFFER_TEMPORARY_NAME] = {177, {}, 0};

    theme.decorations.reserve(5);
    theme.decorations.push(syntax::decoration_line_number());
    theme.decorations.push(syntax::decoration_column_number());
    theme.decorations.push(syntax::decoration_cursor_count());
    theme.decorations.push(syntax::decoration_read_only_indicator());
    theme.decorations.push(syntax::decoration_pinned_indicator());

    theme.overlays.reserve(9);
    theme.overlays.push(syntax::overlay_matching_region({{}, 237, 0}));
    theme.overlays.push(syntax::overlay_preferred_column({{}, 21, 0}));
    theme.overlays.push(syntax::overlay_compiler_messages());
    for (const char* string : {"TODO", "Note", "NOCOMMIT"}) {
        for (Token_Type token_type : {Token_Type::COMMENT, Token_Type::DOC_COMMENT}) {
            theme.overlays.push(syntax::overlay_highlight_string(
                {{}, {}, Face::BOLD}, string, Case_Handling::CASE_SENSITIVE, token_type));
        }
    }

    theme.max_completion_results = 10;
    theme.mini_buffer_max_height = 10;
    theme.mini_buffer_completion_filter = spaces_are_wildcards_completion_filter;
    theme.window_completion_filter = prefix_completion_filter;

    theme.mouse_scroll_rows = 4;
    theme.mouse_scroll_cols = 10;

    theme.draw_line_numbers = false;

    theme.allow_animated_scrolling = true;
    theme.scroll_outside_visual_rows = 3;
    theme.scroll_jump_half_page_when_outside_visible_region = false;

    theme.scroll_outside_visual_columns = 10;

#ifdef _WIN32
    terminal_script = "start pwsh";
#else
    terminal_script = "xterm";
#endif
}

void editor_created_callback(Editor* editor) {
    create_key_remap(editor->key_remap);
    create_key_map(editor->key_map);
    create_theme(editor->theme);
    sort_global_commands();

    register_global_command(COMMAND(command_add_highlight_to_buffer));
    register_global_command(COMMAND(command_remove_highlight_from_buffer));
    register_global_command(COMMAND(ascii_drawing::command_draw_box));

    start_server(editor);
}

static void directory_key_map(Key_Map& key_map) {
    BIND(key_map, "ENTER", command_directory_open_path);
    BIND(key_map, "A-j", command_directory_open_path);
    BIND(key_map, "s", command_directory_run_path);
    BIND(key_map, "d", command_directory_delete_path);
    BIND(key_map, "c", command_directory_copy_path_complete_path);
    BIND(key_map, "C", command_directory_copy_path_complete_directory);
    BIND(key_map, "r", command_directory_rename_path_complete_path);
    BIND(key_map, "R", command_directory_rename_path_complete_directory);
    BIND(key_map, "g", command_directory_reload);
    BIND(key_map, "TAB", command_directory_toggle_sort);
    BIND(key_map, "m", command_create_directory);

    BIND(key_map, "n", command_forward_line);
    BIND(key_map, "p", command_backward_line);
}

static void search_key_map(Key_Map& key_map) {
    BIND(key_map, "ENTER", command_search_buffer_open_selected);
    BIND(key_map, "g", command_search_buffer_reload);

    BIND(key_map, "o", command_search_buffer_open_selected_no_swap);
    BIND(key_map, "n", command_search_buffer_open_next_no_swap);
    BIND(key_map, "p", command_search_buffer_open_previous_no_swap);
    BIND(key_map, "N", command_forward_line);
    BIND(key_map, "P", command_backward_line);
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
    BIND(key_map, "TAB", command_insert_completion);
    BIND(key_map, "A-j", command_insert_completion_and_submit_mini_buffer);
    BIND(key_map, "ENTER", command_submit_mini_buffer);

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

    BIND(key_map, "ENTER", window_completion::command_finish_completion);
    BIND(key_map, "C-c", window_completion::command_finish_completion);
    BIND(key_map, "C-A-c", window_completion::command_finish_completion);
}

static void git_edit_key_map(Key_Map& key_map) {
    BIND(key_map, "A-c A-c", version_control::command_save_and_quit);
    BIND(key_map, "A-c A-k", version_control::command_abort_and_quit);
}

static void hash_comments_key_map(Key_Map& key_map) {
    BIND(key_map, "A-h", basic::command_reformat_comment_hash);
    BIND(key_map, "A-;", basic::command_comment_hash);
    BIND(key_map, "A-:", basic::command_uncomment_hash);

    BIND(key_map, "A-c d 6", hash::command_insert_divider_60);
    BIND(key_map, "A-c d 7", hash::command_insert_divider_70);
    BIND(key_map, "A-c d 8", hash::command_insert_divider_80);
    BIND(key_map, "A-c d 9", hash::command_insert_divider_90);
    BIND(key_map, "A-c d 0", hash::command_insert_divider_100);
    BIND(key_map, "A-c d 1", hash::command_insert_divider_110);
    BIND(key_map, "A-c d 2", hash::command_insert_divider_120);

    BIND(key_map, "A-c h 6", hash::command_insert_header_60);
    BIND(key_map, "A-c h 7", hash::command_insert_header_70);
    BIND(key_map, "A-c h 8", hash::command_insert_header_80);
    BIND(key_map, "A-c h 9", hash::command_insert_header_90);
    BIND(key_map, "A-c h 0", hash::command_insert_header_100);
    BIND(key_map, "A-c h 1", hash::command_insert_header_110);
    BIND(key_map, "A-c h 2", hash::command_insert_header_120);
}

static void indent_based_hierarchy_mode(Mode& mode) {
    mode.discover_indent_policy = Discover_Indent_Policy::COPY_PREVIOUS_LINE;
    BIND(mode.key_map, "C-A-a", command_backward_up_token_pair_or_indent);
    BIND(mode.key_map, "C-A-e", command_forward_up_token_pair_or_indent);
    BIND(mode.key_map, "C-A-A", region_movement::command_backward_up_token_pair_or_indent);
    BIND(mode.key_map, "C-A-E", region_movement::command_forward_up_token_pair_or_indent);
}

static void cpp_comments_key_map(Key_Map& key_map) {
    BIND(key_map, "A-;", cpp::command_comment);
    BIND(key_map, "A-:", cpp::command_uncomment);
    BIND(key_map, "A-h", cpp::command_reformat_comment);

    BIND(key_map, "A-c d 6", cpp::command_insert_divider_60);
    BIND(key_map, "A-c d 7", cpp::command_insert_divider_70);
    BIND(key_map, "A-c d 8", cpp::command_insert_divider_80);
    BIND(key_map, "A-c d 9", cpp::command_insert_divider_90);
    BIND(key_map, "A-c d 0", cpp::command_insert_divider_100);
    BIND(key_map, "A-c d 1", cpp::command_insert_divider_110);
    BIND(key_map, "A-c d 2", cpp::command_insert_divider_120);

    BIND(key_map, "A-c h 6", cpp::command_insert_header_60);
    BIND(key_map, "A-c h 7", cpp::command_insert_header_70);
    BIND(key_map, "A-c h 8", cpp::command_insert_header_80);
    BIND(key_map, "A-c h 9", cpp::command_insert_header_90);
    BIND(key_map, "A-c h 0", cpp::command_insert_header_100);
    BIND(key_map, "A-c h 1", cpp::command_insert_header_110);
    BIND(key_map, "A-c h 2", cpp::command_insert_header_120);
}

static void build_log_mode(Mode& mode) {
    mode.next_token = syntax::build_next_token;

    mode.overlays.reserve(1);
    mode.overlays.push(syntax::overlay_build_severities());

    mode.perform_iteration = basic::build_buffer_iterate;
    BIND(mode.key_map, "ENTER", command_build_open_link_at_point);
    BIND(mode.key_map, "o", command_build_open_link_at_point_no_swap);
    BIND(mode.key_map, "n", command_build_open_next_link_no_swap);
    BIND(mode.key_map, "p", command_build_open_previous_link_no_swap);
    BIND(mode.key_map, "N", command_build_next_link);
    BIND(mode.key_map, "P", command_build_previous_link);

    BIND(mode.key_map, "f", command_build_next_file);
    BIND(mode.key_map, "F", command_build_previous_file);
    BIND(mode.key_map, "e", command_build_next_error);
    BIND(mode.key_map, "E", command_build_previous_error);
}

static void ctest_log_mode(Mode& mode) {
    mode.next_token = syntax::ctest_next_token;
    // I didn't implement iteration and links because the syntax for
    // these will differ based on the logging and test framework.

    BIND(mode.key_map, "f", command_ctest_next_file);
    BIND(mode.key_map, "F", command_ctest_previous_file);
    BIND(mode.key_map, "t", command_ctest_next_test_case);
    BIND(mode.key_map, "T", command_ctest_previous_test_case);
}

/// See if there are any C style comments at start of lines as a simple heuristic.
static bool has_c_style_comments(const Contents& contents) {
    Contents_Iterator it = contents.start();
    while (!it.at_eob() && it.bucket < 10) {
        forward_through_whitespace(&it);
        if (looking_at(it, "//") || looking_at(it, "/*")) {
            return true;
        }
        end_of_line(&it);
        forward_char(&it);
    }
    return false;
}

static const Token_Type standard_matching_identifier_token_types[] = {
    Token_Type::KEYWORD, Token_Type::TYPE, Token_Type::IDENTIFIER};
static void matching_identifier_overlays(
    cz::Heap_Vector<Overlay>* overlays,
    cz::Slice<const Token_Type> types = standard_matching_identifier_token_types) {
    overlays->reserve(2);
    overlays->push(syntax::overlay_matching_pairs({-1, 237, 0}));
    overlays->push(syntax::overlay_matching_tokens({-1, 237, 0}, types));
}

static bool handle_git_show_file(Editor* editor,
                                 Buffer* buffer,
                                 const cz::Arc<Buffer_Handle>& buffer_handle) {
    if (!buffer->name.starts_with("*shell git show ") || !buffer->name.ends_with('*'))
        return false;

    cz::Str args = buffer->name.slice(strlen("*shell git show "), buffer->name.len - 1);
    if (args.contains(' '))
        return false;  // Hard to handle paths with spaces so just ignore.
    cz::Str before, file;
    if (!args.split_excluding(':', &before, &file))
        return false;

    cz::Heap_String path = {};
    CZ_DEFER(path.drop());
    if (file.starts_with("./") || file.starts_with("../")) {
        path.reserve_exact(buffer->directory.len + file.len);
        path.append(buffer->directory);
    } else {
        if (!version_control::get_root_directory(buffer->directory, cz::heap_allocator(), &path))
            return false;  // Not in git, fail.
        path.reserve_exact(1 + file.len);
        path.push('/');
    }
    path.append(file);

    cz::String standardized_path = standardize_path(cz::heap_allocator(), path);
    CZ_DEFER(standardized_path.drop(cz::heap_allocator()));

    cz::Str name, directory;
    Buffer::Type type;
    if (!parse_rendered_buffer_name(standardized_path, &name, &directory, &type)) {
        return false;
    }
    if (type != Buffer::Type::FILE) {
        // Git directory format is totally different & special files can't be produced by git.
        return false;
    }

    reset_mode_as_if(editor, buffer, buffer_handle, name, directory, type);
    return true;
}

void buffer_created_callback(Editor* editor,
                             Buffer* buffer,
                             const cz::Arc<Buffer_Handle>& buffer_handle) {
    ZoneScoped;

    buffer->mode.indent_width = 4;
    buffer->mode.tab_width = 4;
    buffer->mode.use_tabs = false;

    buffer->mode.discover_indent_policy = Discover_Indent_Policy::UP_THEN_BACK_PAIR;

    buffer->mode.indent_after_open_pair = false;

    buffer->mode.search_prompt_case_handling = Case_Handling::SMART_CASE;
    buffer->mode.search_continue_case_handling = Case_Handling::CASE_SENSITIVE;

    buffer->mode.comment_break_tabs = true;
    buffer->mode.tabs_for_alignment = false;

    buffer->mode.preferred_column = 100;

    buffer->mode.wrap_long_lines = false;

    window_completion_key_map(buffer->mode.completion_key_map);

    buffer->mode.next_token = default_next_token;

    bool dynamic_indent_rules = true;

    if (cz::path::has_component(buffer->directory, "mag") ||
        cz::path::has_component(buffer->directory, "cz")) {
        dynamic_indent_rules = false;
        buffer->mode.indent_width = buffer->mode.tab_width = 4;
        buffer->mode.use_tabs = false;
        buffer->mode.preferred_column = 100;
        BIND(buffer->mode.key_map, "F7", command_build_debug_vc_root);

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

    case Buffer::TEMPORARY: {
        // Temporary files pretty much never use tabs and also shouldn't
        // have a max column limit.  They're temporary after all!
        buffer->mode.use_tabs = false;
        buffer->mode.preferred_column = -1;

        if (buffer->name == "*client mini buffer*") {
            buffer->mode.next_token = syntax::path_next_token;
            mini_buffer_key_map(buffer->mode.key_map);
            break;
        } else if (buffer->name == "*scratch*") {
            buffer->mode.overlays.reserve(3);
            buffer->mode.overlays.push(
                syntax::overlay_nearest_matching_identifier_before_after({7, 27, 0}));
            buffer->mode.overlays.push(syntax::overlay_trailing_spaces({{}, 208, 0}));
            buffer->mode.overlays.push(syntax::overlay_incorrect_indent({{}, 208, 0}));
            break;
        }

        // Don't bind "q" in the mini buffer.
        BIND(buffer->mode.key_map, "q", command_quit_window);

        const auto is_shell_command_prefix = [&](cz::Str prefix) {
            return buffer->name.len >= prefix.len + 1 && buffer->name.starts_with(prefix) &&
                   (buffer->name[prefix.len] == ' ' || buffer->name[prefix.len] == '*');
        };

        if (buffer->name.starts_with("*man ")) {
            buffer->mode.next_token = syntax::process_next_token;
        } else if (buffer->name == "*key map*") {
            buffer->mode.next_token = syntax::key_map_next_token;
            BIND(buffer->mode.key_map, "ENTER", command_go_to_key_map_binding);
        } else if (buffer->name == "*splash page*") {
            buffer->mode.next_token = syntax::splash_next_token;
        } else if (buffer->name.starts_with("*diff ")) {
            buffer->mode.next_token = syntax::diff_next_token;
            BIND(buffer->mode.key_map, "g", command_search_buffer_reload);
        } else if (handle_git_show_file(editor, buffer, buffer_handle)) {
            return;
        } else if (buffer->name.starts_with("*git last-edit ") ||
                   buffer->name.starts_with("*git show ") ||
                   buffer->name.starts_with("*git line-history ") ||
                   buffer->name.starts_with("*git log ") || buffer->name == "*git dm*" ||
                   is_shell_command_prefix("*shell git log") ||
                   is_shell_command_prefix("*shell git diff") ||
                   is_shell_command_prefix("*shell git show")) {
            buffer->mode.next_token = syntax::patch_next_token;
            buffer->mode.perform_iteration = version_control::log_buffer_iterate;
            BIND(buffer->mode.key_map, "g", command_search_buffer_reload);

            BIND(buffer->mode.key_map, "s", version_control::command_show_commit_in_log);
            BIND(buffer->mode.key_map, "G", version_control::command_git_log_add_filter);

            BIND(buffer->mode.key_map, "c", version_control::command_git_log_next_commit);
            BIND(buffer->mode.key_map, "C", version_control::command_git_log_previous_commit);
            BIND(buffer->mode.key_map, "f", version_control::command_git_log_next_file);
            BIND(buffer->mode.key_map, "F", version_control::command_git_log_previous_file);

            BIND(buffer->mode.key_map, "o",
                 version_control::command_git_log_open_selected_diff_no_swap);
            BIND(buffer->mode.key_map, "n",
                 version_control::command_git_log_open_next_diff_no_swap);
            BIND(buffer->mode.key_map, "p",
                 version_control::command_git_log_open_previous_diff_no_swap);
            BIND(buffer->mode.key_map, "N", version_control::command_git_log_next_diff);
            BIND(buffer->mode.key_map, "P", version_control::command_git_log_previous_diff);
            BIND(buffer->mode.key_map, "ENTER",
                 version_control::command_git_log_open_selected_commit_or_diff);
        } else if (buffer->name.starts_with("*git blame ")) {
            buffer->mode.next_token = version_control::git_blame_next_token;
            BIND(buffer->mode.key_map, "ENTER", version_control::command_show_commit_in_blame);
            BIND(buffer->mode.key_map, "A-j", version_control::command_show_commit_in_blame);
            BIND(buffer->mode.key_map, "g", version_control::command_blame_reload);
        } else if (buffer->name.starts_with("*git grep ") || buffer->name.starts_with("*ag ") ||
                   buffer->name.starts_with("*shell ")) {
            buffer->mode.next_token = syntax::search_next_token;
            buffer->mode.perform_iteration = basic::search_buffer_iterate;
            search_key_map(buffer->mode.key_map);
        } else if (buffer->name.starts_with("*build ")) {
            build_log_mode(buffer->mode);
            BIND(buffer->mode.key_map, "g", command_search_buffer_reload);
        }
        break;
    }

    case Buffer::FILE: {
        buffer->mode.decorations.reserve(1);
        buffer->mode.decorations.push(syntax::decoration_line_ending_indicator());

        buffer->mode.overlays.reserve(2);
        buffer->mode.overlays.push(
            syntax::overlay_nearest_matching_identifier_before_after({7, 27, 0}));
        buffer->mode.overlays.push(
            syntax::overlay_merge_conflicts({7, 18, 0}, {-1, 52, 0}, {-1, 22, 0}));

        bool add_indent_overlays = true;

        cz::Str name = buffer->name;

        // Treat `#test.c#` as `test.c`.
        if (name.len > 2 && name.starts_with('#') && name.ends_with('#')) {
            name = name.slice(1, name.len - 1);
        }
        // Treat `test.c~` as `test.c`.
        if (name.ends_with('~')) {
            --name.len;
        }

        // Strip off extensions just used to mark files as backups.
        if (name.ends_with(".back")) {
            name.len -= 5;
        }
        if (name.ends_with(".backup")) {
            name.len -= 7;
        }

        // Unwrap decompressed files.
        if (name.ends_with(".zst")) {
            name.len -= 4;
        }
        if (name.ends_with(".gz")) {
            name.len -= 3;
        }

        if (name.ends_with(".c") || name.ends_with(".h") || name.ends_with(".cc") ||
            name.ends_with(".hh") || name.ends_with(".cpp") || name.ends_with(".hpp") ||
            name.ends_with(".cxx") || name.ends_with(".hxx") || name.ends_with(".tpp") ||
            name.ends_with(".glsl") ||
            // Java / C# aren't really C but meh they're close.
            // Note: '.idl' = C# Interface Definition Language.
            name.ends_with(".java") || name.ends_with(".cs") || name.ends_with(".idl")) {
        cpp:
            buffer->mode.next_token = syntax::cpp_next_token;
            BIND(buffer->mode.key_map, "A-x A-f", clang_format::command_clang_format_buffer);
            cpp_comments_key_map(buffer->mode.key_map);
            BIND(buffer->mode.key_map, "A-x *", cpp::command_make_indirect);
            BIND(buffer->mode.key_map, "A-x &", cpp::command_make_direct);
            BIND(buffer->mode.key_map, "A-x e", cpp::command_extract_variable);
            BIND(buffer->mode.key_map, "ENTER", command_insert_newline_split_pairs);
            BIND(buffer->mode.key_map, "A-x y", cpp::command_copy_path_as_include);

            if (name.ends_with(".java")) {
                BIND(buffer->mode.key_map, "A-g A-t", command_java_open_token_at_point);
            }

            static const Token_Type types[] = {
                Token_Type::KEYWORD, Token_Type::TYPE, Token_Type::IDENTIFIER,
                Token_Type::PREPROCESSOR_KEYWORD, Token_Type::PREPROCESSOR_ELSE};
            matching_identifier_overlays(&buffer->mode.overlays, types);
        } else if (name == "CMakeLists.txt" || name.ends_with(".cmake")) {
            buffer->mode.next_token = syntax::cmake_next_token;
            hash_comments_key_map(buffer->mode.key_map);
            BIND(buffer->mode.key_map, "C-A-c",
                 command_complete_at_point_prompt_identifiers_or_cmake_keywords);
            BIND(buffer->mode.key_map, "A-g A-t", prose::command_find_file_prefill_token_at_point);
            indent_based_hierarchy_mode(buffer->mode);
            matching_identifier_overlays(&buffer->mode.overlays);
        } else if (name.ends_with(".md") || name.ends_with(".markdown") ||
                   // .rst / ReStructured Text files aren't
                   // really markdown but they're often pretty similar.
                   name.ends_with(".rst")) {
            buffer->mode.next_token = syntax::md_next_token;
            BIND(buffer->mode.key_map, "A-h", markdown::command_reformat_paragraph);
            BIND(buffer->mode.key_map, "A-x A-f", command_realign_table);
            indent_based_hierarchy_mode(buffer->mode);
        } else if (name.ends_with(".css")) {
            buffer->mode.next_token = syntax::css_next_token;
            BIND(buffer->mode.key_map, "A-h", cpp::command_reformat_comment_block_only);

            static const Token_Type types[] = {
                Token_Type::CSS_PROPERTY, Token_Type::CSS_ELEMENT_SELECTOR,
                Token_Type::CSS_ID_SELECTOR, Token_Type::CSS_CLASS_SELECTOR,
                Token_Type::CSS_PSEUDO_SELECTOR};
            matching_identifier_overlays(&buffer->mode.overlays, types);
        } else if (name.ends_with(".html") || name.ends_with(".xml")) {
            buffer->mode.next_token = syntax::html_next_token;
            BIND(buffer->mode.key_map, "A-;", html::command_comment);
            indent_based_hierarchy_mode(buffer->mode);

            static const Token_Type types[] = {Token_Type::HTML_TAG_NAME,
                                               Token_Type::HTML_ATTRIBUTE_NAME};
            matching_identifier_overlays(&buffer->mode.overlays, types);
        } else if (name.ends_with(".js") || name.ends_with(".ts")) {
        javascript:
            buffer->mode.next_token = syntax::js_next_token;
            cpp_comments_key_map(buffer->mode.key_map);
            matching_identifier_overlays(&buffer->mode.overlays);
        } else if (name.ends_with(".go")) {
            buffer->mode.next_token = syntax::go_next_token;
            cpp_comments_key_map(buffer->mode.key_map);

            // Go uses tabs for alignment.
            buffer->mode.tab_width = buffer->mode.indent_width;
            buffer->mode.use_tabs = true;
            matching_identifier_overlays(&buffer->mode.overlays);
        } else if (name.ends_with(".rs")) {
            buffer->mode.next_token = syntax::rust_next_token;
            cpp_comments_key_map(buffer->mode.key_map);
            BIND(buffer->mode.key_map, "A-x e", rust::command_extract_variable);
            BIND(buffer->mode.key_map, "A-x A-f", rust::command_rust_format_buffer);
            BIND(buffer->mode.key_map, "ENTER", command_insert_newline_split_pairs);
            matching_identifier_overlays(&buffer->mode.overlays);
        } else if (name.ends_with(".zig")) {
            buffer->mode.next_token = syntax::zig_next_token;
            cpp_comments_key_map(buffer->mode.key_map);
            BIND(buffer->mode.key_map, "ENTER", command_insert_newline_split_pairs);
            matching_identifier_overlays(&buffer->mode.overlays);
        } else if (name.ends_with(".proto")) {
            buffer->mode.next_token = syntax::protobuf_next_token;
            cpp_comments_key_map(buffer->mode.key_map);
            BIND(buffer->mode.key_map, "ENTER", command_insert_newline_split_pairs);
            matching_identifier_overlays(&buffer->mode.overlays);

            // Style guide says 2 spaces, 80 characters.
            buffer->mode.indent_width = buffer->mode.tab_width = 2;
            buffer->mode.use_tabs = false;
            buffer->mode.preferred_column = 80;
        } else if (name.ends_with(".sh") || name.ends_with(".bash") || name.ends_with(".zsh") ||
                   name.ends_with(".bashrc") || name == ".teshrc" || name == ".zshrc" ||
                   name == ".profile" || name == ".bash_profile" || name == ".bash_login" ||
                   name == ".bash_logout" ||
                   (buffer->directory == "/etc/" &&
                    (name == "profile" || name == "bash_completion")) ||
                   name == "Makefile" || name == ".gitconfig" || name == ".gitmodules" ||
                   // Powershell isn't really a shell script but it pretty much works.
                   name.ends_with(".ps1") ||
                   // Perl isn't really a shell script but it pretty much works.
                   name.ends_with(".pl") ||
                   // GNU DeBugger isn't really a shell script but it pretty much works.
                   name.ends_with(".gdb")) {
        shell:
            if (name == "Makefile" || name == ".gitconfig" || name == ".gitmodules") {
                // Makefiles must use tabs so set that up automatically.
                buffer->mode.tab_width = buffer->mode.indent_width;
                buffer->mode.use_tabs = true;
                // Indent based on the previous line instead of based
                // on the paren level since there aren't braces.
                indent_based_hierarchy_mode(buffer->mode);
            }

            buffer->mode.next_token = syntax::sh_next_token;
            hash_comments_key_map(buffer->mode.key_map);
            matching_identifier_overlays(&buffer->mode.overlays);
        } else if (name.ends_with(".py")) {
        python:
            buffer->mode.next_token = syntax::python_next_token;
            hash_comments_key_map(buffer->mode.key_map);
            indent_based_hierarchy_mode(buffer->mode);
            matching_identifier_overlays(&buffer->mode.overlays);
        } else if (name.ends_with(".cfg") || name.ends_with(".yaml") || name.ends_with(".toml") ||
                   name.ends_with(".ini") || name == ".editorconfig" || name == ".ignore" ||
                   name == ".gitignore" || name == ".hgignore" || name == ".agignore" ||
                   name == "config") {
        hash_comments:
            // A bunch of miscellaneous file types that all use # for comments.
            buffer->mode.next_token = syntax::general_hash_comments_next_token;
            hash_comments_key_map(buffer->mode.key_map);
            indent_based_hierarchy_mode(buffer->mode);
            matching_identifier_overlays(&buffer->mode.overlays);
        } else if (name.ends_with(".json")) {
            BIND(buffer->mode.key_map, "A-x A-f", javascript::command_jq_format_buffer);
            if (has_c_style_comments(buffer->contents)) {
                // A bunch of miscellaneous file types that all use # for comments.
                buffer->mode.next_token = syntax::general_c_comments_next_token;
                cpp_comments_key_map(buffer->mode.key_map);
                indent_based_hierarchy_mode(buffer->mode);
                matching_identifier_overlays(&buffer->mode.overlays);
            } else {
                goto hash_comments;
            }
        } else if (name.ends_with(".mustache")) {
            buffer->mode.next_token = syntax::mustache_next_token;
            indent_based_hierarchy_mode(buffer->mode);
            matching_identifier_overlays(&buffer->mode.overlays);
        } else if (name.ends_with(".patch") || name.ends_with(".diff")) {
            buffer->mode.next_token = syntax::patch_next_token;
            if (name == "addp-hunk-edit.diff") {
                git_edit_key_map(buffer->mode.key_map);
            }
            add_indent_overlays = false;
            indent_based_hierarchy_mode(buffer->mode);
        } else if (name == "git-rebase-todo") {
            buffer->mode.next_token = syntax::git_rebase_todo_next_token;
            hash_comments_key_map(buffer->mode.key_map);
            git_edit_key_map(buffer->mode.key_map);
            indent_based_hierarchy_mode(buffer->mode);
        } else if (name == "COMMIT_EDITMSG" || name == "MERGE_MSG") {
            buffer->mode.next_token = syntax::git_commit_edit_message_next_token;
            hash_comments_key_map(buffer->mode.key_map);
            BIND(buffer->mode.key_map, "A-h", markdown::command_reformat_paragraph_or_hash_comment);
            git_edit_key_map(buffer->mode.key_map);
            add_indent_overlays = false;
            indent_based_hierarchy_mode(buffer->mode);
        } else if (name == ".vimrc" || name.ends_with(".vim")) {
            buffer->mode.next_token = syntax::vim_script_next_token;
        } else if (name == "build.log") {
            build_log_mode(buffer->mode);
            add_indent_overlays = false;
        } else if (name == "ctest.log") {
            ctest_log_mode(buffer->mode);
            add_indent_overlays = false;
        } else if (name == "color test") {
            buffer->mode.next_token = syntax::color_test_next_token;
            indent_based_hierarchy_mode(buffer->mode);
        } else {
            // Get the first line of the file.
            Contents_Iterator start = buffer->contents.start();
            Contents_Iterator end = start;
            end_of_line(&end);
            SSOStr first_linex = buffer->contents.slice(cz::heap_allocator(), start, end.position);
            CZ_DEFER(first_linex.drop(cz::heap_allocator()));
            cz::Str first_line = first_linex.as_str();

            // Recognize shebangs.
            if (first_line.starts_with("#!")) {
                if (first_line.contains("python")) {
                    goto python;
                } else if (first_line.contains("sh")) {
                    goto shell;
                } else if (first_line.contains("node")) {
                    goto javascript;
                }
            }

            // Recognize Emacs file declarations.
            const char* emacs_end = first_line.rfind("-*-");
            if (emacs_end && first_line.slice_end(emacs_end).contains("-*-")) {
                const char* emacs_start = first_line.slice_end(emacs_end).rfind("-*-") + 3;
                cz::Str emacs_info = first_line.slice(emacs_start, emacs_end);
                if (emacs_info.contains_case_insensitive("python")) {
                    goto python;
                } else if (emacs_info.contains_case_insensitive("shell-script")) {
                    goto shell;
                } else if (emacs_info.contains_case_insensitive("javascript")) {
                    goto javascript;
                } else if (emacs_info.contains_case_insensitive("c++") ||
                           emacs_info.contains_case_insensitive("c")) {
                    goto cpp;
                }
            }

            end = buffer->contents.end();
            while (!end.at_bob()) {
                end.retreat();
                if (end.get() != '\n') {
                    end.advance();
                    break;
                }
            }
            start = end;
            start_of_line(&start);
            SSOStr last_linex = buffer->contents.slice(cz::heap_allocator(), start, end.position);
            CZ_DEFER(last_linex.drop(cz::heap_allocator()));
            cz::Str last_line = last_linex.as_str();

            // Recognize vim file declarations.
            if (last_line.starts_with("# vi: ")) {
                const char* syntax_point = last_line.find("syntax=");
                if (syntax_point) {
                    cz::Str token = last_line.slice_start(syntax_point + strlen("syntax="));
                    token = token.slice_end(token.find_index(' '));
                    if (token == "bash") {
                        goto shell;
                    }
                }
            }

            buffer->mode.next_token = syntax::general_next_token;
            indent_based_hierarchy_mode(buffer->mode);
            matching_identifier_overlays(&buffer->mode.overlays);
        }

        if (add_indent_overlays) {
            buffer->mode.overlays.reserve(2);
            buffer->mode.overlays.push(syntax::overlay_trailing_spaces({{}, 208, 0}));
            buffer->mode.overlays.push(syntax::overlay_incorrect_indent({{}, 208, 0}));
        }

        if (add_indent_overlays && dynamic_indent_rules) {
            parse_indent_rules(buffer->contents, &buffer->mode.indent_width,
                               &buffer->mode.tab_width, &buffer->mode.use_tabs);
        }

        break;
    }
    }
}

}
}
