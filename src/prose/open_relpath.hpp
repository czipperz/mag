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

/// Attempt to open a `arg` that comes from a substring of a buffer in
/// `directory`.  Handles absolute paths, relative paths to the directory, relative
/// paths to vc root, and relative paths to user-configurable directories.
/// See `parse_file_arg_no_disk` for syntax for `arg`.
void open_relpath(Editor* editor, Client* client, cz::Str directory, cz::Str arg);

/// Utility function.  Returns `directory/path` exists.
bool try_relative_to(cz::Str directory, cz::Str path, cz::String* combined);

}
}
