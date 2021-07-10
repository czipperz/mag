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
    Window* window;
    uint32_t row;
    uint32_t column;
};

struct Client {
    bool record_key_presses;
    size_t key_chain_offset;
    cz::Vector<Key> macro_key_chain;

    cz::Vector<Key> key_chain;
    bool queue_quit;

    Copy_Chain* global_copy_chain;
    Jump_Chain jump_chain;

    int (*system_copy_text_func)(void* data, cz::Str text);
    void* system_copy_text_data;
    int system_copy_text(cz::Str text) {
        return system_copy_text_func(system_copy_text_data, text);
    }

    int (*update_global_copy_chain_func)(Copy_Chain** copy_chain, void* data);
    void* update_global_copy_chain_data;
    int update_global_copy_chain();

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
    void set_prompt_text(Editor* editor, cz::Str text);

    /// Show a message to the user.
    void show_message(Editor* editor, cz::Str text);

    /// Show a dialog.  If submitted then `response_callback` is called.  Once the
    /// dialog has ended the data will be deallocated via `cz::heap_allocator`.
    void show_dialog(Editor* editor, Dialog dialog);

    void update_mini_buffer_completion_cache(Editor* editor);

    void hide_mini_buffer(Editor* editor);

    void clear_mini_buffer(Editor* editor);

    void dealloc_message();

    void restore_selected_buffer();
};

}
