#pragma once

#include <cz/str.hpp>
#include "core/file.hpp"

namespace mag {

struct Client;
struct Editor;
struct Buffer;

namespace custom {

/// Called when a client is created.  Currently this only happens at the start of `main()`.
void client_created_callback(Editor* editor, Client* client);

/// Called when the editor is created at the start of `main()`.  The main
/// purpose of this function is to initialize the `Editor::key_map` and
/// `Editor::theme` fields however you may want to do more complicated tasks.
void editor_created_callback(Editor* editor);

/// Called whenever a buffer is created in order to initialize it.  The main
/// purpose of this function is to initialize the `Buffer::mode` field
/// however you may want to do more complicated tasks.
///
/// This method is called before the buffer is installed into the editor.  If the file is loaded
/// via a method from the file module (ie `open_file`) then it will already have its contents
/// filled by the time this method is invoked.  However the contents of the buffer may be loaded
/// after this method is called (for example `prose::command_search_in_current_directory`) so
/// that we can display partial search results to the user.
void buffer_created_callback(Editor* editor, Buffer* buffer);

/// Attempt to find a file based on `path` that comes from a substring of a buffer in `directory`.
/// Note that `vc_dir` and `directory` are automatically tested by `prose::open_relpath`.
bool find_relpath(cz::Option<cz::Str> vc_dir, cz::Str directory, cz::Str path, cz::String* out);

extern bool default_use_carriage_returns;

struct CompressionExtensions {
    const char* extension;
    cz::Str process;
};
extern CompressionExtensions compression_extensions[];
extern size_t compression_extensions_len;

extern bool enable_terminal_colors;
extern bool enable_terminal_mouse;

extern size_t ncurses_batch_paste_boundary;

}
}
