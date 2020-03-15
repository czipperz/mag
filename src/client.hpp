#pragma once

#include <stdlib.h>
#include <chrono>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include "buffer_id.hpp"
#include "key.hpp"
#include "message.hpp"
#include "window.hpp"

namespace mag {

struct Client {
    cz::Vector<Key> key_chain;
    bool queue_quit;

    cz::Vector<Window_Unified*> _offscreen_windows;
    Window* window;
    Window_Unified* selected_normal_window;

    Window_Unified* _mini_buffer;
    bool _select_mini_buffer;

    std::chrono::system_clock::time_point _message_time;
    Message _message;

    void init(Buffer_Id selected_buffer_id, Buffer_Id mini_buffer_id) {
        selected_normal_window = Window_Unified::create(selected_buffer_id);
        window = selected_normal_window;

        _mini_buffer = Window_Unified::create(mini_buffer_id);
    }

    void drop() {
        key_chain.drop(cz::heap_allocator());
        dealloc_message();
        Window::drop_(window);
        Window::drop_(_mini_buffer);
        for (size_t i = 0; i < _offscreen_windows.len(); ++i) {
            Window::drop_(_offscreen_windows[i]);
        }
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

    void show_message(Message message) {
        _message_time = std::chrono::system_clock::now();
        _message = message;
        _select_mini_buffer = message.tag > Message::SHOW;
    }

    void hide_mini_buffer(Editor* editor);

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
