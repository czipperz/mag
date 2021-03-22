#pragma once

#include <stdlib.h>
#include <chrono>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include "buffer_id.hpp"
#include "completion.hpp"
#include "copy_chain.hpp"
#include "jump.hpp"
#include "key.hpp"
#include "message.hpp"
#include "window.hpp"

namespace mag {

struct Client {
    cz::Vector<Key> key_chain;
    bool queue_quit;

    Copy_Chain* global_copy_chain;
    Jump_Chain jump_chain;

    int (*system_copy_text_func)(void* data, cz::Str text);
    void* system_copy_text_data;
    int system_copy_text(cz::Str text) {
        return system_copy_text_func(system_copy_text_data, text);
    }

    cz::Vector<Window_Unified*> _offscreen_windows;
    Window* window;
    Window_Unified* selected_normal_window;

    Window_Unified* _mini_buffer;
    bool _select_mini_buffer;

    Buffer_Id messages_id;

    Completion_Cache mini_buffer_completion_cache;

    std::chrono::system_clock::time_point _message_time;
    Message _message;

    void init(Buffer_Id selected_buffer_id, Buffer_Id mini_buffer_id, Buffer_Id messages_id);
    void drop();

    Window_Unified* mini_buffer_window() const { return _mini_buffer; }

    Window_Unified* selected_window() const {
        if (_select_mini_buffer) {
            return _mini_buffer;
        } else {
            return selected_normal_window;
        }
    }

    Window_Unified* make_window_for_buffer(Buffer_Id id);

    /// Save to offscreen_windows unless it already is
    void save_offscreen_window(Window_Unified* window);

    /// Save to offscreen_windows unless it is on the screen or already in the offscreen_windows
    void save_removed_window(Window_Unified* removed_window);

    void set_selected_buffer(Buffer_Id id);

    void replace_window(Window* o, Window* n);

    void show_message(Editor* editor, cz::Str text);
    void show_dialog(Editor* editor,
                     cz::Str prompt,
                     Completion_Engine completion_engine,
                     Message::Response_Callback response_callback,
                     void* response_callback_data);

    void update_mini_buffer_completion_cache(Editor* editor);

    void hide_mini_buffer(Editor* editor);

    void clear_mini_buffer(Editor* editor);

    /// Call right after running `show_dialog` to populate the mini buffer with the contents of the
    /// selected region in the buffer invoking the dialog.
    void fill_mini_buffer_with_selected_region(Editor* editor);

    void dealloc_message();

    void restore_selected_buffer();
};

}
