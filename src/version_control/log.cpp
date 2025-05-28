#include "log.hpp"

#include <cz/format.hpp>
#include "core/command_macros.hpp"
#include "core/job.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
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
    cz::Arc<Buffer_Handle> handle;
    run_console_command(source.client, editor, root.buffer, args, buffer_name, "Git error",
                        &handle);
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

    cz::Heap_String buffer_name = cz::format("git show ", commit);
    CZ_DEFER(buffer_name.drop());

    cz::Str args[] = {"git", "show", commit};
    cz::Arc<Buffer_Handle> handle;
    run_console_command(client, editor, root.buffer, args, buffer_name, "Git error", &handle);
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

    cz::Heap_String buffer_name = cz::format(show_patch ? "git log -p " : "git log ", path);
    CZ_DEFER(buffer_name.drop());

    cz::Str args_patch[] = {"git", "log", "-p", path};
    cz::Str args_no_patch[] = {"git", "log", path};
    run_console_command(source.client, editor, root.buffer,
                        show_patch ? cz::slice(args_patch) : cz::slice(args_no_patch), buffer_name,
                        "Git error", nullptr);
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
    uint64_t line_number_range[2] = {iterator.get_line_number() + 1};

    if (window->show_marks) {
        iterator.go_to(window->cursors[0].end());
        if (at_start_of_line(iterator) && iterator.position > window->cursors[0].start()) {
            iterator.retreat();
        }
        line_number_range[1] = iterator.get_line_number() + 1;
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

    cz::Heap_String buffer_name = cz::format("git line-history ", path);
    CZ_DEFER(buffer_name.drop());

    cz::Str args[] = {"git", "log", "-L", path};
    run_console_command(source.client, editor, root.buffer, args, buffer_name, "Git error",
                        nullptr);
}

}
}
