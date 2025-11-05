#include "build_commands.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include "basic/search_buffer_commands.hpp"
#include "basic/visible_region_commands.hpp"
#include "core/command_macros.hpp"
#include "core/file.hpp"
#include "core/job.hpp"
#include "core/movement.hpp"
#include "core/token_iterator.hpp"
#include "prose/open_relpath.hpp"
#include "version_control/version_control.hpp"

namespace mag {
namespace basic {

REGISTER_COMMAND(command_build_debug_vc_root);
void command_build_debug_vc_root(Editor* editor, Command_Source source) {
    cz::String top_level_path = {};
    CZ_DEFER(top_level_path.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!version_control::get_root_directory(buffer->directory, cz::heap_allocator(),
                                                 &top_level_path)) {
            source.client->show_message("No version control repository found");
            return;
        }
    }

#ifdef _WIN32
    cz::Str args[] = {"powershell", ".\\build-debug"};
#else
    cz::Str args[] = {"/usr/bin/bash", "-c", "LANG=C ./build-debug"};
#endif
    run_console_command(source.client, editor, top_level_path.buffer, args, "build debug");
}

static void open_result(Editor* editor, Client* client, cz::Str path) {
    if (!client->selected_normal_window->parent || !client->selected_normal_window->parent->fused) {
        Window_Split* split = split_window(client, Window::VERTICAL_SPLIT);
        split->fused = true;
    } else {
        toggle_cycle_window(client);
    }

    open_file_arg(editor, client, path);
}

namespace {
enum class Direction {
    CURRENT,
    NEXT,
    PREVIOUS,
};
}

static bool find_path_in_direction(Editor* editor,
                                   Client* client,
                                   Direction direction,
                                   cz::Heap_String* path_out) {
    WITH_CONST_SELECTED_BUFFER(client);

    if (direction != Direction::CURRENT && (window->cursors.len > 1 || window->show_marks)) {
        Contents_Iterator it =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        basic::iterate_cursors(window, buffer, direction == Direction::NEXT, &it);

        basic::center_selected_cursor(editor, window, buffer);
        direction = Direction::CURRENT;
    }

    Token token;
    Contents_Iterator iterator;
    if (direction == Direction::NEXT) {
        Forward_Token_Iterator token_iterator;
        token_iterator.init_after(buffer, window->cursors[window->selected_cursor].point);
        if (!token_iterator.find_type(Token_Type::BUILD_LOG_LINK)) {
            return false;
        }
        token = token_iterator.token();
        iterator = token_iterator.iterator_at_token_start();
    } else {
        Backward_Token_Iterator token_iterator = {};
        CZ_DEFER(token_iterator.drop(cz::heap_allocator()));
        token_iterator.init_at_or_before(
            cz::heap_allocator(), buffer,
            window->cursors[window->selected_cursor].point - (direction == Direction::PREVIOUS));
        if (!token_iterator.rfind_type(cz::heap_allocator(), Token_Type::BUILD_LOG_LINK)) {
            return false;
        }
        token = token_iterator.token();
        iterator = token_iterator.iterator_at_token_start();
    }

    if (direction != Direction::CURRENT) {
        kill_extra_cursors(window, client);
        window->cursors[window->selected_cursor].point = token.start;
        basic::center_selected_cursor(editor, window, buffer);
    }

    cz::String arg = {};
    CZ_DEFER(arg.drop(cz::heap_allocator()));
    buffer->contents.slice_into(cz::heap_allocator(), iterator, token.end, &arg);

    cz::Str path;
    uint64_t line, column = 0;
    bool has_line = parse_file_arg_no_disk(arg, &path, &line, &column);

    cz::String vc_dir = {};
    CZ_DEFER(vc_dir.drop(cz::heap_allocator()));
    if (!prose::get_relpath(buffer->directory, path, cz::heap_allocator(), path_out, &vc_dir)) {
        return false;
    }
    if (has_line) {
        cz::append(cz::heap_allocator(), path_out, ':', line, ':', column);
    }
    return true;
}

void build_buffer_iterate(Editor* editor, Client* client, bool select_next) {
    cz::Heap_String path = {};
    CZ_DEFER(path.drop());
    if (find_path_in_direction(editor, client, select_next ? Direction::NEXT : Direction::PREVIOUS,
                               &path)) {
        open_result(editor, client, path);
    }
}

REGISTER_COMMAND(command_build_next_link);
void command_build_next_link(Editor* editor, Command_Source source) {
    cz::Heap_String path = {};
    CZ_DEFER(path.drop());
    find_path_in_direction(editor, source.client, Direction::NEXT, &path);
}

REGISTER_COMMAND(command_build_previous_link);
void command_build_previous_link(Editor* editor, Command_Source source) {
    cz::Heap_String path = {};
    CZ_DEFER(path.drop());
    find_path_in_direction(editor, source.client, Direction::PREVIOUS, &path);
}

static void helper(Editor* editor,
                   const Command_Source& source,
                   Direction direction,
                   bool no_swap) {
    cz::Heap_String path = {};
    CZ_DEFER(path.drop());
    if (find_path_in_direction(editor, source.client, direction, &path)) {
        open_result(editor, source.client, path);
        if (no_swap) {
            toggle_cycle_window(source.client);
        }
    }
}
REGISTER_COMMAND(command_build_open_link_at_point);
void command_build_open_link_at_point(Editor* editor, Command_Source source) {
    helper(editor, source, Direction::CURRENT, false);
}
REGISTER_COMMAND(command_build_open_link_at_point_no_swap);
void command_build_open_link_at_point_no_swap(Editor* editor, Command_Source source) {
    helper(editor, source, Direction::CURRENT, true);
}
REGISTER_COMMAND(command_build_open_next_link);
void command_build_open_next_link(Editor* editor, Command_Source source) {
    helper(editor, source, Direction::NEXT, false);
}
REGISTER_COMMAND(command_build_open_next_link_no_swap);
void command_build_open_next_link_no_swap(Editor* editor, Command_Source source) {
    helper(editor, source, Direction::NEXT, true);
}
REGISTER_COMMAND(command_build_open_previous_link);
void command_build_open_previous_link(Editor* editor, Command_Source source) {
    helper(editor, source, Direction::PREVIOUS, false);
}
REGISTER_COMMAND(command_build_open_previous_link_no_swap);
void command_build_open_previous_link_no_swap(Editor* editor, Command_Source source) {
    helper(editor, source, Direction::PREVIOUS, true);
}

////////////////////////////////////////////////////////////////////////////////

void forward_to(Editor* editor, Client* client, Token_Type token_type) {
    WITH_CONST_SELECTED_BUFFER(client);

    Forward_Token_Iterator token_iterator;
    token_iterator.init_after(buffer, window->cursors[window->selected_cursor].point);

    uint64_t position;
    if (token_iterator.find_type(token_type)) {
        position = token_iterator.token().start;

        // If there are multiple in a row then go to the last one.
        while (token_iterator.next() && token_iterator.token().type == token_type) {
            position = token_iterator.token().start;
        }
    } else {
        position = buffer->contents.len;
    }

    kill_extra_cursors(window, client);
    window->cursors[window->selected_cursor].point = position;
    window->start_position = window->cursors[window->selected_cursor].point;
    window->column_offset = 0;
}

void backward_to(Editor* editor, Client* client, Token_Type token_type) {
    WITH_CONST_SELECTED_BUFFER(client);

    Backward_Token_Iterator token_iterator = {};
    CZ_DEFER(token_iterator.drop(cz::heap_allocator()));
    token_iterator.init_before(cz::heap_allocator(), buffer,
                               window->cursors[window->selected_cursor].point);

    // We want to select the last token in each block of the same token and thus if we're already
    // in a block then we should retreat to before it so we can go to the previous block.
    if (token_iterator.has_token() && token_iterator.token().type == token_type) {
        while (token_iterator.previous(cz::heap_allocator()) &&
               token_iterator.token().type == token_type) {
        }
    }

    uint64_t position;
    if (token_iterator.rfind_type(cz::heap_allocator(), token_type)) {
        position = token_iterator.token().start;
    } else {
        position = 0;
    }

    kill_extra_cursors(window, client);
    window->cursors[window->selected_cursor].point = position;
    window->start_position = window->cursors[window->selected_cursor].point;
    window->column_offset = 0;
}

REGISTER_COMMAND(command_build_next_file);
void command_build_next_file(Editor* editor, Command_Source source) {
    forward_to(editor, source.client, Token_Type::BUILD_LOG_FILE_HEADER);
}

REGISTER_COMMAND(command_build_previous_file);
void command_build_previous_file(Editor* editor, Command_Source source) {
    backward_to(editor, source.client, Token_Type::BUILD_LOG_FILE_HEADER);
}

REGISTER_COMMAND(command_ctest_next_file);
void command_ctest_next_file(Editor* editor, Command_Source source) {
    forward_to(editor, source.client, Token_Type::TEST_LOG_FILE_HEADER);
}

REGISTER_COMMAND(command_ctest_previous_file);
void command_ctest_previous_file(Editor* editor, Command_Source source) {
    backward_to(editor, source.client, Token_Type::TEST_LOG_FILE_HEADER);
}

REGISTER_COMMAND(command_ctest_next_test_case);
void command_ctest_next_test_case(Editor* editor, Command_Source source) {
    forward_to(editor, source.client, Token_Type::TEST_LOG_TEST_CASE_HEADER);
}

REGISTER_COMMAND(command_ctest_previous_test_case);
void command_ctest_previous_test_case(Editor* editor, Command_Source source) {
    backward_to(editor, source.client, Token_Type::TEST_LOG_TEST_CASE_HEADER);
}

}
}
