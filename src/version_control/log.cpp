#include "log.hpp"

#include <cz/format.hpp>
#include <cz/process.hpp>
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

        if (!get_root_directory(buffer->directory.buffer, cz::heap_allocator(), &root)) {
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
        if (!get_root_directory(buffer->directory.buffer, cz::heap_allocator(), &root)) {
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

REGISTER_COMMAND(command_show_commit_in_log);
void command_show_commit_in_log(Editor* editor, Command_Source source) {
    SSOStr commit;
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        Contents_Iterator iterator = buffer->contents.iterator_at(window->sel().point);
        if (!rfind(&iterator, "\ncommit ")) {
            source.client->show_message("Couldn't find a commit");
            return;
        }
        iterator.advance(strlen("\ncommit "));
        if (!slice_commit_at_point(source.client, iterator, &commit))
            return;
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

REGISTER_COMMAND(command_git_log_next_diff);
void command_git_log_next_diff(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    for (size_t c = 0; c < window->cursors.len; ++c) {
        Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[c].point);
        if (find(&iterator, "\n@@ "))
            iterator.advance();
        window->cursors[c].point = iterator.position;
    }
}

REGISTER_COMMAND(command_git_log_previous_diff);
void command_git_log_previous_diff(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    for (size_t c = 0; c < window->cursors.len; ++c) {
        Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[c].point);
        backward_char(&iterator);
        if (rfind(&iterator, "\n@@ "))
            iterator.advance();
        window->cursors[c].point = iterator.position;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Open selected diff commands
////////////////////////////////////////////////////////////////////////////////

static bool open_selected_diff(Editor* editor, Client* client) {
    SSOStr path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    uint64_t line;
    {
        WITH_CONST_SELECTED_BUFFER(client);
        Contents_Iterator iterator =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);

        // Find line number.
        {
            if (!rfind(&iterator, "\n@@ "))
                return false;
            iterator.advance();
            Contents_Iterator eol = iterator;
            end_of_line(&eol);
            SSOStr line_contents =
                buffer->contents.slice(cz::heap_allocator(), iterator, eol.position);
            CZ_DEFER(line_contents.drop(cz::heap_allocator()));
            uint64_t before_line, before_len, after_len;
            if (!parse_diff_line_numbers(line_contents.as_str(), &before_line, &before_len,
                                         /*after_line=*/&line, &after_len))
                return false;
        }

        // Find file.
        {
            if (!rfind(&iterator, "\ndiff --git "))
                return false;
            iterator.advance(strlen("\ndiff --git "));
            Contents_Iterator space = iterator;
            if (!find_this_line(&space, ' '))
                return false;
            path = buffer->contents.slice(cz::heap_allocator(), iterator, space.position);
        }
    }

    // Open a vertical split and put the opened file on the right.
    if (!client->selected_normal_window->parent || !client->selected_normal_window->parent->fused) {
        Window_Split* split = split_window(client, Window::VERTICAL_SPLIT);
        split->fused = true;
    } else {
        toggle_cycle_window(client);
    }

    open_file(editor, client, path.as_str());

    {
        WITH_CONST_SELECTED_BUFFER(client);
        kill_extra_cursors(window, client);
        Contents_Iterator iterator = start_of_line_position(buffer->contents, line);
        window->cursors[0].point = iterator.position;
        center_in_window(window, buffer->mode, editor->theme, iterator);
        window->show_marks = false;
    }

    return true;
}

REGISTER_COMMAND(command_git_log_open_selected_diff);
void command_git_log_open_selected_diff(Editor* editor, Command_Source source) {
    open_selected_diff(editor, source.client);
}

REGISTER_COMMAND(command_git_log_open_next_diff);
void command_git_log_open_next_diff(Editor* editor, Command_Source source) {
    command_git_log_next_diff(editor, source);
    open_selected_diff(editor, source.client);
}
REGISTER_COMMAND(command_git_log_open_previous_diff);
void command_git_log_open_previous_diff(Editor* editor, Command_Source source) {
    command_git_log_previous_diff(editor, source);
    open_selected_diff(editor, source.client);
}

void command_git_log_open_selected_diff_no_swap(Editor* editor, Command_Source source) {
    if (open_selected_diff(editor, source.client))
        toggle_cycle_window(source.client);
}
void command_git_log_open_next_diff_no_swap(Editor* editor, Command_Source source) {
    command_git_log_next_diff(editor, source);
    if (open_selected_diff(editor, source.client))
        toggle_cycle_window(source.client);
}
void command_git_log_open_previous_diff_no_swap(Editor* editor, Command_Source source) {
    command_git_log_previous_diff(editor, source);
    if (open_selected_diff(editor, source.client))
        toggle_cycle_window(source.client);
}

////////////////////////////////////////////////////////////////////////////////
// File history
////////////////////////////////////////////////////////////////////////////////

static void command_git_log_common(Editor* editor, Command_Source source, bool show_patch) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    cz::String root = {};
    CZ_DEFER(root.drop(cz::heap_allocator()));
    if (!get_root_directory(buffer->directory.buffer, cz::heap_allocator(), &root)) {
        source.client->show_message("Error: couldn't find vc root");
        return;
    }

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    if (!buffer->get_path(cz::heap_allocator(), &path)) {
        source.client->show_message("Error: couldn't get buffer path");
        return;
    }

    cz::Heap_String command =
        cz::format(show_patch ? "git log -p " : "git log ", cz::Process::escape_arg(path));
    CZ_DEFER(command.drop());
    run_console_command(source.client, editor, root.buffer, command.buffer, command);
}

REGISTER_COMMAND(command_git_log);
void command_git_log(Editor* editor, Command_Source source) {
    command_git_log_common(editor, source, false);
}

REGISTER_COMMAND(command_file_history);
void command_file_history(Editor* editor, Command_Source source) {
    command_git_log_common(editor, source, true);
}

////////////////////////////////////////////////////////////////////////////////
// Line history
////////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_line_history);
void command_line_history(Editor* editor, Command_Source source) {
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

    cz::String root = {};
    CZ_DEFER(root.drop(cz::heap_allocator()));
    if (!get_root_directory(buffer->directory.buffer, cz::heap_allocator(), &root)) {
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
    cz::append(cz::heap_allocator(), &path, line_number_range[0], ',', line_number_range[1], ':');
    if (!buffer->get_path(cz::heap_allocator(), &path)) {
        source.client->show_message("Error: couldn't get buffer path");
        return;
    }

    cz::Heap_String command = cz::format("git log -L ", cz::Process::escape_arg(path));
    CZ_DEFER(command.drop());
    run_console_command(source.client, editor, root.buffer, command.buffer, command);
}

////////////////////////////////////////////////////////////////////////////////
// Grep history
////////////////////////////////////////////////////////////////////////////////

static void command_git_log_add_filter_callback(Editor* editor,
                                                Client* client,
                                                cz::Str query,
                                                void*) {
    WITH_CONST_SELECTED_BUFFER(client);
    if (buffer->type != Buffer::TEMPORARY || !buffer->name.starts_with("*git log ") ||
        !buffer->name.ends_with("*")) {
        client->show_message("Must be ran from a *git log ...* buffer");
        return;
    }

    cz::Str old_command = buffer->name.slice(1, buffer->name.len - 1);
    cz::Heap_String new_command =
        cz::format(old_command.slice_end(strlen("git log ")), "-G ", cz::Process::escape_arg(query),
                   " ", old_command.slice_start(strlen("git log ")));
    CZ_DEFER(new_command.drop());
    run_console_command(client, editor, buffer->directory.buffer, new_command.buffer, new_command);
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

}
}
