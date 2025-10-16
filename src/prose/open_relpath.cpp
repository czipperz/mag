#include "open_relpath.hpp"

#include <cz/path.hpp>
#include "core/command_macros.hpp"
#include "core/file.hpp"
#include "core/movement.hpp"
#include "custom/config.hpp"
#include "prose/find_file.hpp"
#include "version_control/version_control.hpp"

namespace mag {
namespace prose {

REGISTER_COMMAND(command_open_token_at_relpath);
void command_open_token_at_relpath(Editor* editor, Command_Source source) {
    ZoneScoped;

    SSOStr query = {};
    CZ_DEFER(query.drop(cz::heap_allocator()));

    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(source.client);

        if (!get_token_at_position_contents(buffer, window->cursors[window->selected_cursor].point,
                                            &query)) {
            source.client->show_message("Cursor is not positioned at a token");
            return;
        }

        directory = buffer->directory.clone_null_terminate(cz::heap_allocator());
    }

    if (!prose::open_token_as_relpath(editor, source.client, directory, query.as_str()))
        open_relpath(editor, source.client, directory, query.as_str());
}

bool open_token_as_relpath(Editor* editor, Client* client, cz::Str directory, cz::Str query) {
    bool is_string = (query.starts_with('"') && query.ends_with('"')) ||
                     (query.starts_with('\'') && query.ends_with('\'')) ||
                     (query.starts_with('<') && query.ends_with('>'));
    if (is_string) {
        query = query.slice(1, query.len - 1);
    }
    if (is_string || query.contains(".") || query.contains('/')) {
        open_relpath(editor, client, directory, query);
        return true;
    }
    return false;
}

static void push_jump_and_open_file(Editor* editor,
                                    Client* client,
                                    cz::Str path,
                                    bool has_line,
                                    uint64_t line,
                                    uint64_t column) {
    {
        WITH_CONST_SELECTED_BUFFER(client);
        push_jump(window, client, buffer);
    }
    if (has_line) {
        open_file_at(editor, client, path, line, column);
    } else {
        open_file(editor, client, path);
    }
}

void open_relpath(Editor* editor, Client* client, cz::Str directory, cz::Str arg) {
    cz::Str path;
    uint64_t line, column = 0;
    bool has_line = parse_file_arg_no_disk(arg, &path, &line, &column);

    if (cz::path::is_absolute(path)) {
        return push_jump_and_open_file(editor, client, path, has_line, line, column);
    }

    cz::String temp = {};
    CZ_DEFER(temp.drop(cz::heap_allocator()));
    if (try_relative_to(directory, path, &temp)) {
        return push_jump_and_open_file(editor, client, temp, has_line, line, column);
    }

    cz::String vc_root = {};
    CZ_DEFER(vc_root.drop(cz::heap_allocator()));
    if (version_control::get_root_directory(directory, cz::heap_allocator(), &vc_root)) {
        if (custom::find_relpath(vc_root.as_str(), directory, path, &temp)) {
            return push_jump_and_open_file(editor, client, temp, has_line, line, column);
        }

        if (try_relative_to(vc_root, path, &temp)) {
            return push_jump_and_open_file(editor, client, temp, has_line, line, column);
        }

        vc_root.reserve_exact(cz::heap_allocator(), 2);
        vc_root.push('/');
        vc_root.null_terminate();
        find_file(client, "Find file in version control: ", arg, vc_root);
        vc_root = {};
    } else {
        if (custom::find_relpath({}, directory, path, &temp)) {
            return push_jump_and_open_file(editor, client, temp, has_line, line, column);
        }

        find_file(client, "Find file in current directory: ", arg,
                  directory.clone(cz::heap_allocator()));
    }
}

bool try_relative_to(cz::Str directory, cz::Str path, cz::String* out) {
    out->len = 0;
    cz::path::make_absolute(path, directory, cz::heap_allocator(), out);
    return cz::file::exists(out->buffer);
}

}
}
