#pragma once

#include <cz/arc.hpp>
#include <cz/str.hpp>
#include "core/file.hpp"
#include "gnu_global/generic.hpp"

namespace mag {

struct Client;
struct Editor;
struct Buffer;
struct Buffer_Handle;

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
void buffer_created_callback(Editor* editor,
                             Buffer* buffer,
                             const cz::Arc<Buffer_Handle>& buffer_handle);

void console_command_finished_callback(Editor* editor, Client* client,
                                       const cz::Arc<Buffer_Handle>& buffer_handle);

void rendering_frame_callback(Editor* editor, Client* client);

bool should_run_clang_tidy(const Buffer* buffer);
bool clang_tidy_script(cz::Str vc_root, const Buffer* buffer, cz::Heap_String* script);

bool should_hide_buffer_from_completion(const Buffer* buffer);

/// Attempt to find a file based on `path` that comes from a substring of a buffer in `directory`.
/// Note that `vc_dir` and `directory` are automatically tested by `prose::open_relpath`.
/// These functions are tried from top to bottom.  The first one to return true will be opened.
bool find_relpath_in_directory(cz::Str directory, cz::Str path, cz::String* out);
bool find_relpath_in_vc(cz::Str vc_dir, cz::Str directory, cz::Str path, cz::String* out);
bool find_relpath_globally(cz::Str path, cz::String* out);

/// Attempt to find a tags file to generate completion in the folder
/// `directory`.  Use `tags::try_directory` to test a source directory.
bool find_tags(cz::Str directory, tags::Engine* engine, cz::String* found_directory);

extern bool default_use_carriage_returns;

struct CompressionExtensions {
    bool (*matches)(cz::Str path);
    cz::Str process;
};
extern CompressionExtensions compression_extensions[];
extern size_t compression_extensions_len;

extern bool enable_terminal_colors;
extern bool enable_terminal_mouse;

extern size_t ncurses_batch_paste_boundary;

}
}
