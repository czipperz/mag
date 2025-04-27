#pragma once

namespace cz {
struct Input_File;
}

namespace mag {
struct Client;
struct Buffer;

/// Apply a diff file to the buffer.
///
/// Returns an error message to show to the user or `nullptr` on success.
const char* apply_diff_file(Buffer* buffer, cz::Input_File file);

/// Reload the file if it has changed.
///
/// Returns an error message to show to the user or `nullptr` on success.
const char* reload_file(Buffer* buffer);

}
