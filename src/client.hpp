#pragma once

#include <stdlib.h>
#include <chrono>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include "buffer_id.hpp"
#include "key.hpp"
#include "message.hpp"

namespace mag {

struct Window {
    Window* parent;

    enum Tag {
        UNIFIED,
        VERTICAL_SPLIT,
        HORIZONTAL_SPLIT,
    } tag;
    union {
        Buffer_Id unified_id;
        struct {
            Window* left;
            Window* right;
        } vertical_split;
        struct {
            Window* top;
            Window* bottom;
        } horizontal_split;
    } v;

    static Window* alloc();
    static Window* create(Buffer_Id buffer_id);
    static void drop(Window* window);
};

struct Client {
    cz::Vector<Key> key_chain;
    bool queue_quit;

    Window* window;
    Window* _selected_window;

    Buffer_Id _mini_buffer_id;
    bool _select_mini_buffer;

    std::chrono::system_clock::time_point _message_time;
    Message _message;

    void init(Buffer_Id selected_buffer_id, Buffer_Id mini_buffer_id) {
        window = Window::create(selected_buffer_id);
        _selected_window = window;
        _mini_buffer_id = mini_buffer_id;
    }

    void drop() {
        key_chain.drop(cz::heap_allocator());
        dealloc_message();
        Window::drop(window);
    }

    Buffer_Id mini_buffer_id() const { return _mini_buffer_id; }
    Buffer_Id selected_buffer_id() const {
        if (_select_mini_buffer) {
            return _mini_buffer_id;
        } else {
            Window* w = _selected_window;
            CZ_DEBUG_ASSERT(w->tag == Window::UNIFIED);
            return w->v.unified_id;
        }
    }

    void show_message(Message message) {
        _message_time = std::chrono::system_clock::now();
        _message = message;
        _select_mini_buffer = message.tag > Message::SHOW;
    }

    void hide_mini_buffer() {
        restore_selected_buffer();
        dealloc_message();
    }

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
