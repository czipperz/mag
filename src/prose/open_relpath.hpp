#pragma once

#include <cz/string.hpp>

namespace mag {
struct Editor;
struct Client;

namespace prose {

/// If query looks like a path then try to open it and return true.
bool open_token_as_relpath(Editor* editor, Client* client, cz::Str directory, cz::Str query);

/// Attempt to open a `path` that comes from a substring of a buffer in
/// `directory`.  Handles absolute paths, relative paths to the directory, relative
/// paths to vc root, and relative paths to user-configurable directories.
void open_relpath(Editor* editor, Client* client, cz::Str directory, cz::Str path);

/// Utility function.  If `directory/path` exists then opens it and returns true.
bool try_relative_to(Editor* editor,
                     Client* client,
                     cz::Str directory,
                     cz::Str path,
                     cz::String* temp);

}
}
