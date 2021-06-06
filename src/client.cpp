#include "client.hpp"

#include <stdio.h>
#include <cz/date.hpp>
#include "command_macros.hpp"
#include "editor.hpp"

namespace mag {

int Client::update_global_copy_chain() {
    return update_global_copy_chain_func(&global_copy_chain, update_global_copy_chain_data);
}

void Client::init(Buffer_Id selected_buffer_id, Buffer_Id mini_buffer_id, Buffer_Id messages_id) {
    selected_normal_window = Window_Unified::create(selected_buffer_id);
    window = selected_normal_window;

    _mini_buffer = Window_Unified::create(mini_buffer_id);
    mini_buffer_completion_cache.init();

    this->messages_id = messages_id;
}

void Client::drop() {
    macro_key_chain.drop(cz::heap_allocator());
    key_chain.drop(cz::heap_allocator());
    jump_chain.drop();
    dealloc_message();
    Window::drop_(window);
    Window::drop_(_mini_buffer);
    for (size_t i = 0; i < _offscreen_windows.len(); ++i) {
        Window::drop_(_offscreen_windows[i]);
    }
    _offscreen_windows.drop(cz::heap_allocator());
    mini_buffer_completion_cache.drop();
}

void Client::hide_mini_buffer(Editor* editor) {
    restore_selected_buffer();
    if (_message.response_cancel) {
        _message.response_cancel(editor, this, _message.response_callback_data);
    }
    dealloc_message();
    clear_mini_buffer(editor);
}

void Client::clear_mini_buffer(Editor* editor) {
    Window_Unified* window = mini_buffer_window();
    WITH_WINDOW_BUFFER(window);
    clear_buffer(buffer);
}

void Client::dealloc_message() {
    cz::heap_allocator().dealloc({_message.response_callback_data, 0});
    _message.response_callback = nullptr;
    _message.interactive_response_callback = nullptr;
    _message.response_cancel = nullptr;
    _message.response_callback_data = nullptr;
}

void Client::restore_selected_buffer() {
    _select_mini_buffer = false;
    _message.tag = Message::NONE;
}

static bool binary_search_offscreen_windows(cz::Slice<Window_Unified*> offscreen_windows,
                                            Buffer_Id id,
                                            size_t* index) {
    size_t start = 0;
    size_t end = offscreen_windows.len;
    while (start < end) {
        size_t mid = (start + end) / 2;
        Window_Unified* w = offscreen_windows[mid];
        CZ_DEBUG_ASSERT(w->tag == Window::UNIFIED);
        if (w->id == id) {
            *index = mid;
            return true;
        } else if (w->id.value < id.value) {
            start = mid + 1;
        } else {
            end = mid;
        }
    }

    *index = start;
    return false;
}

static bool find_matching_window(Window* w, Buffer_Id id, Window_Unified** out) {
    switch (w->tag) {
    case Window::UNIFIED: {
        Window_Unified* window = (Window_Unified*)w;
        if (window->id == id) {
            *out = window;
            return true;
        } else {
            return false;
        }
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)w;
        return find_matching_window(window->first, id, out) ||
               find_matching_window(window->second, id, out);
    }
    }

    CZ_PANIC("");
}

Window_Unified* Client::make_window_for_buffer(Buffer_Id id) {
    Window_Unified* window;

    size_t index;
    if (binary_search_offscreen_windows(_offscreen_windows, id, &index)) {
        window = _offscreen_windows[index];
        _offscreen_windows.remove(index);
        return window;
    }

    if (find_matching_window(this->window, id, &window)) {
        return window->clone();
    } else {
        return Window_Unified::create(id);
    }
}

void Client::save_offscreen_window(Window_Unified* window) {
    size_t index;
    if (binary_search_offscreen_windows(_offscreen_windows, window->id, &index)) {
        // Delete the window because another window is already saved
        Window::drop_(window);
    } else {
        window->pinned = false;
        _offscreen_windows.reserve(cz::heap_allocator(), 1);
        _offscreen_windows.insert(index, window);
    }
}

void Client::save_removed_window(Window_Unified* removed_window) {
    Window_Unified* matching_window;
    if (find_matching_window(this->window, removed_window->id, &matching_window)) {
        // Delete the window because another window is currently open
        Window::drop_(removed_window);
    } else {
        // Store the window for later in the offscreen windows
        save_offscreen_window(removed_window);
    }
}

void Client::set_selected_buffer(Buffer_Id id) {
    if (selected_window()->id == id) {
        return;
    }

    Window_Unified* old_selected_window = selected_normal_window;
    selected_normal_window = make_window_for_buffer(id);
    replace_window(old_selected_window, selected_normal_window);
    save_removed_window(old_selected_window);
}

void Client::replace_window(Window* o, Window* n) {
    if (o->parent) {
        CZ_DEBUG_ASSERT(o->parent->tag == Window::VERTICAL_SPLIT ||
                        o->parent->tag == Window::HORIZONTAL_SPLIT);
        if (o->parent->first == o) {
            o->parent->first = n;
            n->parent = o->parent;
        } else {
            CZ_DEBUG_ASSERT(o->parent->second == o);
            o->parent->second = n;
            n->parent = o->parent;
        }
    } else {
        this->window = n;
        n->parent = nullptr;
    }

    n->set_size(o->rows, o->cols);
}

void Client::set_prompt_text(Editor* editor, cz::Str text) {
    WITH_BUFFER(messages_id);

    cz::Date date = cz::time_t_to_date_local(time(nullptr));
    char date_string[32];
    snprintf(date_string, sizeof(date_string), "%04d/%02d/%02d %02d:%02d:%02d: ", date.year,
             date.month, date.day_of_month, date.hour, date.minute, date.second);
    buffer->contents.append(date_string);

    _message_time = std::chrono::system_clock::now();
    _message.start = buffer->contents.len;
    _message.end = buffer->contents.len + text.len;

    buffer->contents.append(text);
    buffer->contents.append("\n");
}

void Client::show_message(Editor* editor, cz::Str text) {
    _message = {};
    set_prompt_text(editor, text);
    _message.tag = Message::SHOW;
    _select_mini_buffer = false;
}

static void setup_completion_cache(Client* client, Editor* editor) {
    ZoneScoped;

    Completion_Cache* completion_cache = &client->mini_buffer_completion_cache;

    if (client->_message.tag <= Message::SHOW) {
        completion_cache->engine = nullptr;
        return;
    }

    Completion_Engine engine = client->_message.completion_engine;
    completion_cache->set_engine(engine);

    client->update_mini_buffer_completion_cache(editor);

    // Reset the state because it may get updated by updating the completion cache.
    completion_cache->state = Completion_Cache::INITIAL;
}

void Client::update_mini_buffer_completion_cache(Editor* editor) {
    WITH_WINDOW_BUFFER(mini_buffer_window());

    if (mini_buffer_completion_cache.update(buffer->changes.len())) {
        buffer->contents.stringify_into(cz::heap_allocator(),
                                        &mini_buffer_completion_cache.engine_context.query);
    }
}

void Client::show_dialog(Editor* editor,
                         cz::Str prompt,
                         Completion_Engine completion_engine,
                         Message::Response_Callback response_callback,
                         void* response_callback_data) {
    show_interactive_dialog(editor, prompt, completion_engine, response_callback, nullptr, nullptr,
                            response_callback_data);
}

void Client::show_interactive_dialog(Editor* editor,
                                     cz::Str prompt,
                                     Completion_Engine completion_engine,
                                     Message::Response_Callback response_callback,
                                     Message::Response_Callback interactive_response_callback,
                                     Message::Response_Cancel response_cancel,
                                     void* response_callback_data) {
    dealloc_message();
    clear_mini_buffer(editor);

    show_message(editor, prompt);
    _message.tag = Message::RESPOND;
    _message.completion_engine = completion_engine;
    _message.response_callback = response_callback;
    _message.interactive_response_callback = interactive_response_callback;
    _message.response_cancel = response_cancel;
    _message.response_callback_data = response_callback_data;
    _select_mini_buffer = true;

    setup_completion_cache(this, editor);
}

void Client::fill_mini_buffer_with_selected_region(Editor* editor) {
    SSOStr value;

    {
        Window_Unified* window = selected_normal_window;
        WITH_CONST_WINDOW_BUFFER(window);
        if (!window->show_marks) {
            return;
        }

        uint64_t start = window->cursors[0].start();
        uint64_t end = window->cursors[0].end();
        value =
            buffer->contents.slice(cz::heap_allocator(), buffer->contents.iterator_at(start), end);
    }

    {
        WITH_WINDOW_BUFFER(mini_buffer_window());

        Transaction transaction = {};
        transaction.init(buffer);
        CZ_DEFER(transaction.drop());

        Edit edit;
        edit.value = value;
        edit.position = 0;
        edit.flags = Edit::INSERT;
        transaction.push(edit);

        transaction.commit();
    }
}

}
