#pragma once

#include <stdlib.h>
#include <chrono>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include "completion.hpp"
#include "copy_chain.hpp"
#include "dialog.hpp"
#include "jump.hpp"
#include "key.hpp"
#include "message.hpp"
#include "window.hpp"

namespace mag {

struct Mouse_Position {
    bool pressed_buttons[5];

    bool has_client_position;
    uint32_t client_row;
    uint32_t client_column;

    Window* window;  // optional
    uint32_t window_row;
    uint32_t window_column;

    // The place the user started selecting.
    uint32_t selecting;
    uint64_t window_select_point;
};

struct Client {
    enum {
        NCURSES,
        SDL,
        REMOTE,
    } type;

    bool record_key_presses;
    size_t key_chain_offset;
    cz::Vector<Key> macro_key_chain;

    cz::Vector<Key> key_chain;
    bool queue_quit;

    Copy_Chain* global_copy_chain;
    Jump_Chain jump_chain;

    /// Set the system clipboard's value.
    bool (*set_system_clipboard_func)(void* data, cz::Str text);
    void* set_system_clipboard_data;
    bool set_system_clipboard(cz::Str text) {
        if (!set_system_clipboard_func)
            return false;
        return set_system_clipboard_func(set_system_clipboard_data, text);
    }

    /// Get the system clipboard's value.
    bool (*get_system_clipboard_func)(void* data, cz::Allocator allocator, cz::String* string);
    void* get_system_clipboard_data;
    bool get_system_clipboard(cz::Allocator allocator, cz::String* string) {
        if (!get_system_clipboard_func)
            return false;
        return get_system_clipboard_func(get_system_clipboard_data, allocator, string);
    }

    /// If the system clipboard has changed then push it onto the global copy chain.
    bool update_global_copy_chain(Editor* editor);

    Mouse_Position mouse;

    cz::Vector<Window_Unified*> _offscreen_windows;
    Window* window;
    Window_Unified* selected_normal_window;

    Window_Unified* _mini_buffer;
    bool _select_mini_buffer;

    cz::Arc<Buffer_Handle> messages_buffer_handle;

    Completion_Cache mini_buffer_completion_cache;

    std::chrono::system_clock::time_point _message_time;
    Message _message;

    bool _pending_raise;

    /// Clones all inputs.
    void init(cz::Arc<Buffer_Handle> selected_buffer_handle,
              cz::Arc<Buffer_Handle> mini_buffer_handle,
              cz::Arc<Buffer_Handle> messages_handle);
    void drop();

    Window_Unified* mini_buffer_window() const { return _mini_buffer; }

    Window_Unified* selected_window() const {
        if (_select_mini_buffer) {
            return _mini_buffer;
        } else {
            return selected_normal_window;
        }
    }

    /// Clones the handle.
    Window_Unified* make_window_for_buffer(cz::Arc<Buffer_Handle> buffer_handle);

    /// Save to offscreen_windows unless it already is.
    void save_offscreen_window(Window_Unified* window);

    /// Save to offscreen_windows unless it is on the screen or already in the offscreen_windows.
    void save_removed_window(Window_Unified* removed_window);

    /// Clones the handle.
    void set_selected_buffer(cz::Arc<Buffer_Handle> buffer_handle);

    void replace_window(Window* o, Window* n);

    /// Set the prompt's text without changing the callbacks.
    void set_prompt_text(cz::Str text);

    /// Show a message to the user.
    void show_message(cz::Str text);

    /// Show a dialog.  If submitted then `response_callback` is called.  Once the
    /// dialog has ended the data will be deallocated via `cz::heap_allocator`.
    void show_dialog(Dialog dialog);

    void update_mini_buffer_completion_cache();

    void hide_mini_buffer(Editor* editor);

    void clear_mini_buffer(Editor* editor);

    void dealloc_message();

    void restore_selected_buffer();

    void raise() { _pending_raise = true; }
};

}
