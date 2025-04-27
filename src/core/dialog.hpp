#pragma once

#include <cz/str.hpp>
#include <cz/string.hpp>
#include "core/completion.hpp"
#include "core/message.hpp"
#include "core/token.hpp"

namespace mag {

/// See `Client::show_dialog`.  All fields may be left as default or specified.
///
/// Example:
///
/// ```
/// Client* client = /*...*/;
/// Dialog dialog = {};
/// dialog.prompt = "Open file: ";
/// dialog.completion_engine = file_completion_engine;
/// dialog.response_callback = command_open_file_callback;
/// client->show_dialog(dialog);
/// ```
struct Dialog {
    /// The text to be shown before the mini buffer.  Example: `"Open file: "`.
    cz::Str prompt;

    /// The completion engine to service completion with.
    Completion_Engine completion_engine;

    /// If the prompt is submitted then the callback is called.
    Message::Response_Callback response_callback;

    /// This callback will be called every frame to allow for showing interactive prompts.
    ///
    /// For example, `command_search_forward` will show the result if one exists.
    Message::Response_Callback interactive_response_callback;

    /// If the prompt is cancelled then this callback is called.
    Message::Response_Cancel response_cancel;

    /// The data to be passed to the callbacks above.
    void* response_callback_data;

    /// The string to set the mini buffer to.
    cz::Str mini_buffer_contents;

    /// The tokenizer to use for syntax highlighting; if
    /// unset then no syntax highlighting will be performed.
    Tokenizer next_token;
};

//////////////////////////////////////////////////////////
/// Common methods for getting `mini_buffer_contents`. ///
//////////////////////////////////////////////////////////

struct Buffer;
struct Client;
struct Editor;
struct Window_Unified;

/// Get the selected region in the specified window and buffer as a string.
void get_selected_region(Window_Unified* window,
                         const Buffer* buffer,
                         cz::Allocator allocator,
                         cz::String* string);

/// Get the selected region in the selected window as a string.
void get_selected_region(Editor* editor,
                         Client* client,
                         cz::Allocator allocator,
                         cz::String* string);

/// Get the selected window's buffer's directory or, if the buffer
/// has no directory, then retrieves the working directory.
void get_selected_window_directory(Editor* editor,
                                   Client* client,
                                   cz::Allocator allocator,
                                   cz::String* string);

}
