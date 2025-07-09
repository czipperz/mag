#include "open_relpath.hpp"

#include <cz/path.hpp>
#include "core/file.hpp"
#include "custom/config.hpp"
#include "prose/find_file.hpp"
#include "version_control/version_control.hpp"

namespace mag {
namespace prose {

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

void open_relpath(Editor* editor, Client* client, cz::Str directory, cz::Str path) {
    if (cz::path::is_absolute(path)) {
        open_file(editor, client, path);
        return;
    }

    cz::String temp = {};
    CZ_DEFER(temp.drop(cz::heap_allocator()));
    if (try_relative_to(editor, client, directory, path, &temp)) {
        return;
    }

    cz::String vc_root = {};
    CZ_DEFER(vc_root.drop(cz::heap_allocator()));
    if (version_control::get_root_directory(directory, cz::heap_allocator(), &vc_root)) {
        if (custom::open_relpath(editor, client, vc_root.as_str(), directory, path, &temp)) {
            return;
        }

        if (try_relative_to(editor, client, vc_root, path, &temp)) {
            return;
        }

        vc_root.reserve_exact(cz::heap_allocator(), 2);
        vc_root.push('/');
        vc_root.null_terminate();
        find_file(client, "Find file in version control: ", path, vc_root);
        vc_root = {};
    } else {
        if (custom::open_relpath(editor, client, {}, directory, path, &temp)) {
            return;
        }

        find_file(client, "Find file in current directory: ", path,
                  directory.clone(cz::heap_allocator()));
    }
}

bool try_relative_to(Editor* editor,
                     Client* client,
                     cz::Str directory,
                     cz::Str path,
                     cz::String* temp) {
    temp->len = 0;
    cz::path::make_absolute(path, directory, cz::heap_allocator(), temp);
    if (cz::file::exists(temp->buffer)) {
        open_file(editor, client, *temp);
        return true;
    } else {
        return false;
    }
}

}
}
