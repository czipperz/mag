#include "log.hpp"

#include <cz/format.hpp>
#include <cz/process.hpp>
#include "basic/search_buffer_commands.hpp"
#include "basic/visible_region_commands.hpp"
#include "core/command_macros.hpp"
#include "core/file.hpp"
#include "core/job.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/visible_region.hpp"
#include "line_numbers_before_diff.hpp"
#include "version_control.hpp"

namespace mag {
namespace version_control {

///////////////////////////////////////////////////////////////////////////////
// command_show_last_commit_to_file
///////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_show_last_commit_to_file);
void command_show_last_commit_to_file(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    cz::String root = {};
    CZ_DEFER(root.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!buffer->get_path(cz::heap_allocator(), &path)) {
            source.client->show_message("Error: file has no path");
            return;
        }

        if (!get_root_directory(buffer->directory, cz::heap_allocator(), &root)) {
            source.client->show_message("Error: couldn't find vc root");
            return;
        }
    }

    cz::Heap_String buffer_name = cz::format("git last-edit ", path);
    CZ_DEFER(buffer_name.drop());

    cz::Str args[] = {"git", "log", "-1", "-p", "--full-diff", "--", path};
    run_console_command(source.client, editor, root.buffer, args, buffer_name);
}

///////////////////////////////////////////////////////////////////////////////
// command_show_commit
///////////////////////////////////////////////////////////////////////////////

static void command_show_commit_callback(Editor* editor, Client* client, cz::Str commit, void*) {
    cz::String root = {};
    CZ_DEFER(root.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(client);
        if (!get_root_directory(buffer->directory, cz::heap_allocator(), &root)) {
            client->show_message("Error: couldn't find vc root");
            return;
        }
    }

    cz::Heap_String command = cz::format("git show ", cz::Process::escape_arg(commit));
    CZ_DEFER(command.drop());
    run_console_command(client, editor, root.buffer, command.buffer, command);
}

REGISTER_COMMAND(command_show_commit);
void command_show_commit(Editor* editor, Command_Source source) {
    cz::String selected_region = {};
    CZ_DEFER(selected_region.drop(cz::heap_allocator()));
    get_selected_region(editor, source.client, cz::heap_allocator(), &selected_region);

    Dialog dialog = {};
    dialog.prompt = "Show commit: ";
    dialog.response_callback = command_show_commit_callback;
    dialog.mini_buffer_contents = selected_region;
    source.client->show_dialog(dialog);
}

static bool slice_commit_at_point(Client* client, Contents_Iterator iterator, SSOStr* commit) {
    Contents_Iterator test = iterator;
    for (uint64_t i = 0; i < 8; ++i) {
        if (test.at_eob() || !cz::is_hex_digit(test.get())) {
            client->show_message("No commit on this line");
            return false;
        }
        test.advance();
    }
    *commit = iterator.contents->slice(cz::heap_allocator(), iterator, iterator.position + 8);
    return true;
}

REGISTER_COMMAND(command_show_commit_in_blame);
void command_show_commit_in_blame(Editor* editor, Command_Source source) {
    SSOStr commit;
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        Contents_Iterator iterator = buffer->contents.iterator_at(window->sel().point);
        start_of_line(&iterator);
        if (!slice_commit_at_point(source.client, iterator, &commit))
            return;
    }
    CZ_DEFER(commit.drop(cz::heap_allocator()));
    command_show_commit_callback(editor, source.client, commit.as_str(), nullptr);
}

static bool get_commit_at_point(Client* client, Contents_Iterator iterator, SSOStr* commit) {
    if (!rfind(&iterator, "\ncommit ")) {
        client->show_message("Couldn't find a commit");
        return false;
    }
    iterator.advance(strlen("\ncommit "));
    if (!slice_commit_at_point(client, iterator, commit))
        return false;
    return true;
}

REGISTER_COMMAND(command_show_commit_in_log);
void command_show_commit_in_log(Editor* editor, Command_Source source) {
    SSOStr commit;
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!get_commit_at_point(source.client, buffer->contents.iterator_at(window->sel().point),
                                 &commit)) {
            return;
        }
    }
    CZ_DEFER(commit.drop(cz::heap_allocator()));
    command_show_commit_callback(editor, source.client, commit.as_str(), nullptr);
}

////////////////////////////////////////////////////////////////////////////////
// Movement commands
////////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_git_log_next_commit);
void command_git_log_next_commit(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    for (size_t c = 0; c < window->cursors.len; ++c) {
        Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[c].point);
        if (find(&iterator, "\ncommit "))
            iterator.advance();
        window->cursors[c].point = iterator.position;
    }
    window->start_position = window->cursors[window->selected_cursor].point;
    window->column_offset = 0;
}

REGISTER_COMMAND(command_git_log_previous_commit);
void command_git_log_previous_commit(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    for (size_t c = 0; c < window->cursors.len; ++c) {
        Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[c].point);
        backward_char(&iterator);
        if (rfind(&iterator, "\ncommit "))
            iterator.advance();
        window->cursors[c].point = iterator.position;
    }
    window->start_position = window->cursors[window->selected_cursor].point;
    window->column_offset = 0;
}

REGISTER_COMMAND(command_git_log_next_file);
void command_git_log_next_file(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    for (size_t c = 0; c < window->cursors.len; ++c) {
        Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[c].point);
        if (find(&iterator, "\ndiff --git "))
            iterator.advance();
        window->cursors[c].point = iterator.position;
    }
    window->start_position = window->cursors[window->selected_cursor].point;
    window->column_offset = 0;
}

REGISTER_COMMAND(command_git_log_previous_file);
void command_git_log_previous_file(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    for (size_t c = 0; c < window->cursors.len; ++c) {
        Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[c].point);
        backward_char(&iterator);
        if (rfind(&iterator, "\ndiff --git "))
            iterator.advance();
        window->cursors[c].point = iterator.position;
    }
    window->start_position = window->cursors[window->selected_cursor].point;
    window->column_offset = 0;
}

static void git_log_iterate_diff(Editor* editor,
                                 Window_Unified* window,
                                 const Buffer* buffer,
                                 bool select_next) {
    if (window->cursors.len > 1 || window->show_marks) {
        Contents_Iterator it =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        if (!basic::iterate_cursors(window, buffer, select_next, &it))
            return;
    } else {
        for (size_t c = 0; c < window->cursors.len; ++c) {
            Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[c].point);
            if (!select_next)
                backward_char(&iterator);
            if (select_next ? find(&iterator, "\n@@ ") : rfind(&iterator, "\n@@ "))
                iterator.advance();
            window->cursors[c].point = iterator.position;
        }
    }
    basic::center_selected_cursor(editor, window, buffer);
}

REGISTER_COMMAND(command_git_log_next_diff);
void command_git_log_next_diff(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    git_log_iterate_diff(editor, window, buffer, /*select_next=*/true);
}
REGISTER_COMMAND(command_git_log_previous_diff);
void command_git_log_previous_diff(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    git_log_iterate_diff(editor, window, buffer, /*select_next=*/false);
}

////////////////////////////////////////////////////////////////////////////////
// Open selected diff commands
////////////////////////////////////////////////////////////////////////////////

static bool in_diff_file_header(Contents_Iterator iterator) {
    size_t retreat = 0;
    if (looking_at(iterator, "---"))
        retreat = 1;
    if (looking_at(iterator, "+++"))
        retreat = 2;
    for (size_t i = 0; i < retreat; ++i) {
        backward_char(&iterator);
        start_of_line(&iterator);
    }
    return looking_at(iterator, "diff --git ") || looking_at(iterator, "new file mode ") ||
           looking_at(iterator, "old mode ") || looking_at(iterator, "new mode ") ||
           looking_at(iterator, "index ");
}

static bool find_line_number(Contents_Iterator iterator, uint64_t* line, uint64_t* column) {
    Contents_Iterator sol_cursor = iterator;
    start_of_line(&sol_cursor);
    *column = iterator.position - sol_cursor.position;

    if (in_diff_file_header(sol_cursor)) {
        *line = 1;
        *column = 1;
        return true;
    }

    if (!rfind(&iterator, "\n@@ "))
        return false;
    iterator.advance();
    Contents_Iterator eol = iterator;
    end_of_line(&eol);
    SSOStr line_contents = iterator.contents->slice(cz::heap_allocator(), iterator, eol.position);
    CZ_DEFER(line_contents.drop(cz::heap_allocator()));
    uint64_t before_line, before_len, after_len;
    if (!parse_diff_line_numbers(line_contents.as_str(), &before_line, &before_len,
                                 /*after_line=*/line, &after_len))
        return false;

    if (iterator.position == sol_cursor.position) {
        // Cursor is at the '@@' line.  Go to the first changed line in the diff region.
        while (1) {
            end_of_line(&iterator);
            forward_char(&iterator);
            if (iterator.at_eob())
                break;
            if (iterator.get() != ' ')
                break;
            ++*line;
        }
        *column = 1;
    } else {
        // Cursor is at a line in the diff.  Try to go to it.
        while (1) {
            end_of_line(&iterator);
            forward_char(&iterator);
            if (iterator.position >= sol_cursor.position)
                break;
            if (iterator.get() != '-')
                ++*line;
        }
        if (looking_at(sol_cursor, "-"))
            *column = 1;
    }
    return true;
}

static bool in_commit_message_or_header(Contents_Iterator it) {
    Contents_Iterator commit = it;
    if (!rfind(&commit, "\ncommit "))
        return false;
    Contents_Iterator temp = commit;
    return !(find_before(&temp, it.position, "\ndiff --git") ||
             find_before(&commit, it.position, "\n@@ "));
}

namespace enums {
enum SelectedCommitOrDiff {
    SELECTED_COMMIT_OR_DIFF,
    SELECTED_DIFF,
    NEXT_DIFF,
    PREVIOUS_DIFF,
};
enum SelectedCommitOrDiffResult {
    FAILURE,
    COMMIT,
    DIFF,
};
}
using enums::SelectedCommitOrDiff;
using enums::SelectedCommitOrDiffResult;

static SelectedCommitOrDiffResult get_selected_commit_or_diff(Editor* editor,
                                                              Client* client,
                                                              const Buffer* buffer,
                                                              Window_Unified* window,
                                                              SelectedCommitOrDiff select_next,
                                                              cz::Heap_String* path,
                                                              uint64_t* line,
                                                              uint64_t* column,
                                                              SSOStr* commit) {
    if (select_next == SelectedCommitOrDiff::SELECTED_COMMIT_OR_DIFF ||
        select_next == SelectedCommitOrDiff::SELECTED_DIFF) {
        Contents_Iterator iterator =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        if (in_commit_message_or_header(iterator)) {
            if (select_next == SelectedCommitOrDiff::SELECTED_COMMIT_OR_DIFF) {
                if (get_commit_at_point(client, iterator, commit))
                    return SelectedCommitOrDiffResult::COMMIT;
            }
            return SelectedCommitOrDiffResult::FAILURE;
        }
    } else {
        git_log_iterate_diff(editor, window, buffer,
                             select_next == SelectedCommitOrDiff::NEXT_DIFF);
    }

    Contents_Iterator iterator =
        buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);

    if (!find_line_number(iterator, line, column))
        return SelectedCommitOrDiffResult::FAILURE;

    // Find file.
    {
        if (!rfind(&iterator, "\ndiff --git "))
            return SelectedCommitOrDiffResult::FAILURE;
        iterator.advance(strlen("\ndiff --git "));
        if (!find_this_line(&iterator, ' '))
            return SelectedCommitOrDiffResult::FAILURE;
        iterator.advance();
        if (looking_at(iterator, "a/") || looking_at(iterator, "b/"))
            iterator.advance(2);
        Contents_Iterator eol = iterator;
        end_of_line(&eol);
        cz::append(path, buffer->directory);
        buffer->contents.slice_into(cz::heap_allocator(), iterator, eol.position, path);
    }

    return SelectedCommitOrDiffResult::DIFF;
}

static SelectedCommitOrDiffResult open_selected_commit_or_diff(Editor* editor,
                                                               Client* client,
                                                               SelectedCommitOrDiff select_next) {
    cz::Heap_String path = {};
    CZ_DEFER(path.drop());
    uint64_t line, column;
    SSOStr commit = {};
    CZ_DEFER(commit.drop(cz::heap_allocator()));
    SelectedCommitOrDiffResult result;
    {
        WITH_CONST_SELECTED_BUFFER(client);
        result = get_selected_commit_or_diff(editor, client, buffer, window, select_next, &path,
                                             &line, &column, &commit);
    }

    if (result == SelectedCommitOrDiffResult::FAILURE)
        return result;

    if (result == SelectedCommitOrDiffResult::COMMIT) {
        command_show_commit_callback(editor, client, commit.as_str(), nullptr);
        return result;
    }

    // Open a vertical split and put the opened file on the right.
    if (!client->selected_normal_window->parent || !client->selected_normal_window->parent->fused) {
        Window_Split* split = split_window(client, Window::VERTICAL_SPLIT);
        split->fused = true;
    } else {
        toggle_cycle_window(client);
    }

    open_file_at(editor, client, path, line, column);

    return result;
}

REGISTER_COMMAND(command_git_log_open_selected_commit_or_diff);
void command_git_log_open_selected_commit_or_diff(Editor* editor, Command_Source source) {
    open_selected_commit_or_diff(editor, source.client,
                                 SelectedCommitOrDiff::SELECTED_COMMIT_OR_DIFF);
}

REGISTER_COMMAND(command_git_log_open_selected_diff);
void command_git_log_open_selected_diff(Editor* editor, Command_Source source) {
    open_selected_commit_or_diff(editor, source.client, SelectedCommitOrDiff::SELECTED_DIFF);
}
REGISTER_COMMAND(command_git_log_open_next_diff);
void command_git_log_open_next_diff(Editor* editor, Command_Source source) {
    open_selected_commit_or_diff(editor, source.client, SelectedCommitOrDiff::NEXT_DIFF);
}
REGISTER_COMMAND(command_git_log_open_previous_diff);
void command_git_log_open_previous_diff(Editor* editor, Command_Source source) {
    open_selected_commit_or_diff(editor, source.client, SelectedCommitOrDiff::PREVIOUS_DIFF);
}

void command_git_log_open_selected_diff_no_swap(Editor* editor, Command_Source source) {
    if (open_selected_commit_or_diff(editor, source.client, SelectedCommitOrDiff::SELECTED_DIFF) ==
        SelectedCommitOrDiffResult::DIFF) {
        toggle_cycle_window(source.client);
    }
}
void command_git_log_open_next_diff_no_swap(Editor* editor, Command_Source source) {
    if (open_selected_commit_or_diff(editor, source.client, SelectedCommitOrDiff::NEXT_DIFF) ==
        SelectedCommitOrDiffResult::DIFF) {
        toggle_cycle_window(source.client);
    }
}
void command_git_log_open_previous_diff_no_swap(Editor* editor, Command_Source source) {
    if (open_selected_commit_or_diff(editor, source.client, SelectedCommitOrDiff::PREVIOUS_DIFF) ==
        SelectedCommitOrDiffResult::DIFF) {
        toggle_cycle_window(source.client);
    }
}

void log_buffer_iterate(Editor* editor, Client* client, bool select_next) {
    open_selected_commit_or_diff(
        editor, client,
        select_next ? SelectedCommitOrDiff::NEXT_DIFF : SelectedCommitOrDiff::PREVIOUS_DIFF);
}

////////////////////////////////////////////////////////////////////////////////
// File history
////////////////////////////////////////////////////////////////////////////////

static void command_git_log_common(Editor* editor,
                                   Client* client,
                                   cz::Str filter,
                                   bool show_patch) {
    cz::String root = {};
    CZ_DEFER(root.drop(cz::heap_allocator()));
    cz::Heap_String command = {};
    CZ_DEFER(command.drop());

    {
        WITH_CONST_SELECTED_BUFFER(client);

        if (!get_root_directory(buffer->directory, cz::heap_allocator(), &root)) {
            client->show_message("Error: couldn't find vc root");
            return;
        }

        cz::String path = {};
        CZ_DEFER(path.drop(cz::heap_allocator()));
        if (!buffer->get_path(cz::heap_allocator(), &path)) {
            client->show_message("Error: couldn't get buffer path");
            return;
        }

        cz::append(&command, "git log ");
        if (filter.len > 0)
            cz::append(&command, "-G ", cz::Process::escape_arg(filter), " ");
        if (show_patch)
            cz::append(&command, "-p ");
        cz::append(&command, cz::Process::escape_arg(path));
        command.reserve_exact(1);
        command.null_terminate();
    }

    run_console_command(client, editor, root.buffer, command.buffer, command);
}

REGISTER_COMMAND(command_git_log);
void command_git_log(Editor* editor, Command_Source source) {
    command_git_log_common(editor, source.client, "", false);
}

REGISTER_COMMAND(command_file_history);
void command_file_history(Editor* editor, Command_Source source) {
    command_git_log_common(editor, source.client, "", true);
}

static void command_file_history_filtered_callback(Editor* editor,
                                                   Client* client,
                                                   cz::Str filter,
                                                   void* _data) {
    command_git_log_common(editor, client, filter, true);
}

REGISTER_COMMAND(command_file_history_filtered);
void command_file_history_filtered(Editor* editor, Command_Source source) {
    cz::String selected_region = {};
    CZ_DEFER(selected_region.drop(cz::heap_allocator()));
    Dialog dialog = {};
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        dialog.next_token = buffer->mode.next_token;
        get_selected_region(window, buffer, cz::heap_allocator(), &selected_region);
    }
    dialog.prompt = "Filter: ";
    dialog.response_callback = command_file_history_filtered_callback;
    dialog.mini_buffer_contents = selected_region;
    source.client->show_dialog(dialog);
}

////////////////////////////////////////////////////////////////////////////////
// Line history
////////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_line_history);
void command_line_history(Editor* editor, Command_Source source) {
    cz::String root = {};
    CZ_DEFER(root.drop(cz::heap_allocator()));
    cz::Heap_String command = {};
    CZ_DEFER(command.drop());

    {
        WITH_CONST_SELECTED_BUFFER(source.client);

        if (buffer->type != Buffer::FILE) {
            source.client->show_message("Error: buffer must be a file");
            return;
        }

        Contents_Iterator iterator = buffer->contents.iterator_at(
            window->show_marks ? window->cursors[0].start() : window->cursors[0].point);
        uint64_t line_number_range[2] = {iterator.get_line_number()};

        if (window->show_marks) {
            iterator.go_to(window->cursors[0].end());
            if (at_start_of_line(iterator) && iterator.position > window->cursors[0].start()) {
                iterator.retreat();
            }
            line_number_range[1] = iterator.get_line_number();
        } else {
            line_number_range[1] = line_number_range[0];
        }

        if (!get_root_directory(buffer->directory, cz::heap_allocator(), &root)) {
            source.client->show_message("Error: couldn't find vc root");
            return;
        }

        if (!line_numbers_before_changes_to_path(buffer->directory.buffer, buffer->name,
                                                 line_number_range)) {
            source.client->show_message("Error: couldn't calculate line numbers before diff");
            return;
        }

        cz::String path = {};
        CZ_DEFER(path.drop(cz::heap_allocator()));
        cz::append(cz::heap_allocator(), &path, line_number_range[0], ',', line_number_range[1],
                   ':');
        if (!buffer->get_path(cz::heap_allocator(), &path)) {
            source.client->show_message("Error: couldn't get buffer path");
            return;
        }
        command = cz::format("git log -L ", cz::Process::escape_arg(path));
    }

    run_console_command(source.client, editor, root.buffer, command.buffer, command);
}

////////////////////////////////////////////////////////////////////////////////
// Grep history
////////////////////////////////////////////////////////////////////////////////

static bool extract_shell_command_if_prefix_matches(const Buffer* buffer,
                                                    cz::Str prefix,
                                                    cz::Str* old_command_suffix) {
    if (buffer->type != Buffer::TEMPORARY || !buffer->name.starts_with('*') ||
        !buffer->name.ends_with('*') || buffer->name.len < 2) {
        return false;
    }

    cz::Str command = buffer->name.slice(1, buffer->name.len - 1);
    if (command.starts_with("shell "))
        command = command.slice_start(strlen("shell "));

    if (!command.starts_with(prefix))
        return false;
    command = command.slice_start(prefix.len);

    if (command.len == 0 || command[0] == ' ') {
        *old_command_suffix = command;
        return true;
    }
    return false;
}

static void command_git_log_add_filter_callback(Editor* editor,
                                                Client* client,
                                                cz::Str query,
                                                void*) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));
    cz::Heap_String new_command = {};
    CZ_DEFER(new_command.drop());

    {
        WITH_CONST_SELECTED_BUFFER(client);
        cz::Str old_command_suffix;
        if (!extract_shell_command_if_prefix_matches(buffer, "git log", &old_command_suffix)) {
            client->show_message("Must be ran from a *git log ...* buffer");
            return;
        }

        new_command = cz::format("git log -G ", cz::Process::escape_arg(query), old_command_suffix);

        directory = buffer->directory.clone_null_terminate_or_propagate_null(cz::heap_allocator());
    }

    run_console_command(client, editor, directory.buffer, new_command.buffer, new_command);
}

REGISTER_COMMAND(command_git_log_add_filter);
void command_git_log_add_filter(Editor* editor, Command_Source source) {
    cz::String selected_region = {};
    CZ_DEFER(selected_region.drop(cz::heap_allocator()));
    get_selected_region(editor, source.client, cz::heap_allocator(), &selected_region);

    Dialog dialog = {};
    dialog.prompt = "git log add filter: ";
    dialog.response_callback = command_git_log_add_filter_callback;
    dialog.mini_buffer_contents = selected_region;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_git_log_add_follow);
void command_git_log_add_follow(Editor* editor, Command_Source source) {
    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));
    cz::Heap_String new_command = {};
    CZ_DEFER(new_command.drop());

    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        cz::Str old_command_suffix;
        if (!extract_shell_command_if_prefix_matches(buffer, "git log", &old_command_suffix)) {
            source.client->show_message("Must be ran from a *git log ...* buffer");
            return;
        }

        new_command = cz::format("git log --follow", old_command_suffix);

        directory = buffer->directory.clone_null_terminate_or_propagate_null(cz::heap_allocator());
    }

    run_console_command(source.client, editor, directory.buffer, new_command.buffer, new_command);
}

REGISTER_COMMAND(command_git_diff_master);
void command_git_diff_master(Editor* editor, Command_Source source) {
    cz::String root = {};
    CZ_DEFER(root.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!get_root_directory(buffer->directory, cz::heap_allocator(), &root)) {
            source.client->show_message("Error: couldn't find vc root");
            return;
        }
    }

    run_console_command(
        source.client, editor, root.buffer,
        "git diff \"$(git merge-base "
        "origin/\"$(git symbolic-ref refs/remotes/origin/HEAD | sed 's@^refs/remotes/origin/@@')\""
        " HEAD)\"",
        "git dm");
}

}
}
