#pragma once

#include <cz/string.hpp>

namespace mag {
struct Editor;
struct Client;
struct Command_Source;

namespace prose {

void command_open_token_at_relpath(Editor* editor, Command_Source source);

/// If query looks like a path then try to open it and return true.
bool open_token_as_relpath(Editor* editor, Client* client, cz::Str directory, cz::Str query);

/// If `get_relpath` finds a result then opens it.  See `parse_file_arg_no_disk`
/// for syntax for `arg`; the path component is passed to `get_relpath`.
/// Otherwise starts an interactive `find_file` prompt.
void open_relpath(Editor* editor, Client* client, cz::Str directory, cz::Str arg);

/// Attempt to locate `path` that comes from a substring of a buffer in
/// `directory`.  Handles absolute paths, relative paths to the directory, relative
/// paths to vc root, and relative paths to user-configurable directories.
/// `vc_dir` is null terminated and does not have a trailing forward slash.
bool get_relpath(cz::Str directory,
                 cz::Str path,
                 cz::Allocator allocator,
                 /*out*/ cz::String* found_path,
                 /*out*/ cz::String* vc_root);

/// Utility function.  Returns `directory/path` exists.
bool try_relative_to(cz::Str directory, cz::Str path, cz::String* combined);

}
}
