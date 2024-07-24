#pragma once

#include <cz/str.hpp>
#include "src/file.hpp"

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

/// Called whenever a file is being loaded.  This function should plug in
/// different decompression mechanisms.  `path` can be freely modified.
Load_File_Result load_file_callback(Buffer* buffer, cz::Input_File file, cz::String* path);

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

extern bool default_use_carriage_returns;

}
}
