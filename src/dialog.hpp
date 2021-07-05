#pragma once

#include <cz/str.hpp>
#include "completion.hpp"
#include "message.hpp"

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
};

}
