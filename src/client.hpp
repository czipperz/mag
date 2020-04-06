#pragma once

#include <stdlib.h>
#include <chrono>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include "buffer_id.hpp"
#include "completion_results.hpp"
#include "copy_chain.hpp"
#include "key.hpp"
#include "message.hpp"
#include "window.hpp"

namespace mag {

struct Client {
    cz::Vector<Key> key_chain;
    bool queue_quit;

    Copy_Chain* global_copy_chain;

    cz::Vector<Window_Unified*> _offscreen_windows;
    Window* window;
    Window_Unified* selected_normal_window;

    Window_Unified* _mini_buffer;
    bool _select_mini_buffer;

    Completion_Cache mini_buffer_completion_cache;

    std::chrono::system_clock::time_point _message_time;
    Message _message;

    void init(Buffer_Id selected_buffer_id, Buffer_Id mini_buffer_id) {
        selected_normal_window = Window_Unified::create(selected_buffer_id);
        window = selected_normal_window;

        _mini_buffer = Window_Unified::create(mini_buffer_id);
        mini_buffer_completion_cache.init();
    }

    void drop() {
        key_chain.drop(cz::heap_allocator());
        dealloc_message();
        Window::drop_(window);
        Window::drop_(_mini_buffer);
        for (size_t i = 0; i < _offscreen_windows.len(); ++i) {
            Window::drop_(_offscreen_windows[i]);
        }
        _offscreen_windows.drop(cz::heap_allocator());
        mini_buffer_completion_cache.drop();
    }

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

    void show_message(cz::Str text) {
        _message_time = std::chrono::system_clock::now();
        _message = {};
        _message.text = text;
        _message.tag = Message::SHOW;
        _select_mini_buffer = false;
    }

    void show_dialog(Editor* editor,
                     cz::Str prompt,
                     Completion_Engine completion_engine,
                     Message::Response_Callback response_callback,
                     void* response_callback_data) {
        dealloc_message();
        clear_mini_buffer(editor);

        _message_time = std::chrono::system_clock::now();
        _message = {};
        _message.text = prompt;
        _message.tag = Message::RESPOND;
        _message.completion_engine = completion_engine;
        _message.response_callback = response_callback;
        _message.response_callback_data = response_callback_data;
        _select_mini_buffer = true;
    }

    void hide_mini_buffer(Editor* editor) {
        restore_selected_buffer();
        dealloc_message();
        clear_mini_buffer(editor);
    }

    void clear_mini_buffer(Editor* editor);

    void dealloc_message() {
        free(_message.response_callback_data);
        _message.response_callback_data = nullptr;
    }

    void restore_selected_buffer() {
        _select_mini_buffer = false;
        _message.tag = Message::NONE;
    }
};

}
