#include "client.hpp"

#include <stdio.h>
#include <cz/date.hpp>
#include "core/command_macros.hpp"
#include "core/editor.hpp"

namespace mag {

bool Client::update_global_copy_chain(Editor* editor) {
    cz::String text = {};
    CZ_DEFER(text.drop(cz::heap_allocator()));

    if (!get_system_clipboard(cz::heap_allocator(), &text)) {
        return false;
    }

    bool push = true;
    if (global_copy_chain) {
        push = (text != global_copy_chain->value.as_str());
    }

    if (push) {
        Copy_Chain* chain = editor->copy_buffer.allocator().alloc<Copy_Chain>();
        chain->value = SSOStr::as_duplicate(editor->copy_buffer.allocator(), text);
        chain->previous = global_copy_chain;
        global_copy_chain = chain;
    }

    return true;
}

void Client::init(cz::Arc<Buffer_Handle> selected_buffer_handle,
                  cz::Arc<Buffer_Handle> mini_buffer_handle,
                  cz::Arc<Buffer_Handle> messages_handle) {
    next_window_id = 1;
    selected_normal_window = Window_Unified::create(selected_buffer_handle, next_window_id++);
    window = selected_normal_window;

    _mini_buffer = Window_Unified::create(mini_buffer_handle, next_window_id++);
    mini_buffer_completion_cache.init();

    messages_buffer_handle = messages_handle.clone();
}

void Client::drop() {
    macro_key_chain.drop(cz::heap_allocator());
    key_chain.drop(cz::heap_allocator());
    jump_chain.drop();
    dealloc_message();
    Window::drop_(window);
    Window::drop_(_mini_buffer);
    for (size_t i = 0; i < _offscreen_windows.len; ++i) {
        Window::drop_(_offscreen_windows[i]);
    }
    _offscreen_windows.drop(cz::heap_allocator());
    messages_buffer_handle.drop();
    mini_buffer_completion_cache.drop();
}

void Client::hide_mini_buffer(Editor* editor) {
    restore_selected_buffer();
    if (_message.response_cancel) {
        _message.response_cancel(editor, this, _message.response_callback_data);
    }
    dealloc_message();
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
                                            cz::Arc<Buffer_Handle> buffer_handle,
                                            size_t* index) {
    size_t start = 0;
    size_t end = offscreen_windows.len;
    while (start < end) {
        size_t mid = (start + end) / 2;
        Window_Unified* w = offscreen_windows[mid];
        CZ_DEBUG_ASSERT(w->tag == Window::UNIFIED);
        if (w->buffer_handle.get() == buffer_handle.get()) {
            *index = mid;
            return true;
        } else if (w->buffer_handle.get() < buffer_handle.get()) {
            start = mid + 1;
        } else {
            end = mid;
        }
    }

    *index = start;
    return false;
}

bool find_window_for_buffer(Window* w, cz::Arc<Buffer_Handle> buffer_handle, Window_Unified** out) {
    switch (w->tag) {
    case Window::UNIFIED: {
        Window_Unified* window = (Window_Unified*)w;
        if (window->buffer_handle.get() == buffer_handle.get()) {
            *out = window;
            return true;
        } else {
            return false;
        }
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)w;
        return find_window_for_buffer(window->first, buffer_handle, out) ||
               find_window_for_buffer(window->second, buffer_handle, out);
    }
    }

    CZ_PANIC("");
}

Window_Unified* Client::make_window_for_buffer(cz::Arc<Buffer_Handle> buffer_handle) {
    Window_Unified* window;

    size_t index;
    if (binary_search_offscreen_windows(_offscreen_windows, buffer_handle, &index)) {
        window = _offscreen_windows[index];
        _offscreen_windows.remove(index);
        return window;
    }

    if (find_window_for_buffer(this->window, buffer_handle, &window)) {
        return window->clone(next_window_id++);
    } else {
        return Window_Unified::create(buffer_handle, next_window_id++);
    }
}

void Client::save_offscreen_window(Window_Unified* window) {
    size_t index;
    if (binary_search_offscreen_windows(_offscreen_windows, window->buffer_handle, &index)) {
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
    if (find_window_for_buffer(this->window, removed_window->buffer_handle, &matching_window)) {
        // Delete the window because another window is currently open
        Window::drop_(removed_window);
    } else {
        // Store the window for later in the offscreen windows
        save_offscreen_window(removed_window);
    }
}

void Client::set_selected_buffer(cz::Arc<Buffer_Handle> buffer_handle) {
    if (selected_window()->buffer_handle.get() == buffer_handle.get()) {
        return;
    }

    Window_Unified* old_selected_window = selected_normal_window;
    selected_normal_window = make_window_for_buffer(buffer_handle);
    replace_window(old_selected_window, selected_normal_window);
    save_removed_window(old_selected_window);
}

void Client::close_fused_paired_windows() {
    Window_Unified* selected = selected_normal_window;
    Window_Split* parent = selected->parent;
    if (parent && parent->fused) {
        Window* other_child = (selected == parent->first ? parent->second : parent->first);
        replace_window(parent, selected);
        recursively_save_removed_window(other_child);
    }
}

void Client::recursively_save_removed_window(Window* window) {
    if (window->tag == Window::UNIFIED) {
        save_removed_window((Window_Unified*)window);
    } else {
        Window_Split* split = (Window_Split*)window;
        recursively_save_removed_window(split->first);
        recursively_save_removed_window(split->second);
        Window_Split::drop_non_recursive(split);
    }
}

void Client::replace_window(const Window* o, Window* n) {
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

    n->set_size(o->total_rows, o->total_cols);
}

void Client::set_prompt_text(cz::Str text) {
    WITH_BUFFER_HANDLE(messages_buffer_handle);

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

void Client::show_message(cz::Str text) {
    _message = {};
    set_prompt_text(text);
    _message.tag = Message::SHOW;
    _select_mini_buffer = false;
}

static void setup_completion_cache(Client* client) {
    ZoneScoped;

    Completion_Cache* completion_cache = &client->mini_buffer_completion_cache;

    if (client->_message.tag <= Message::SHOW) {
        completion_cache->engine = nullptr;
        return;
    }

    Completion_Engine engine = client->_message.completion_engine;
    completion_cache->set_engine(engine);

    client->update_mini_buffer_completion_cache();

    // Reset the state because it may get updated by updating the completion cache.
    completion_cache->state = Completion_Cache::INITIAL;
}

void Client::update_mini_buffer_completion_cache() {
    WITH_WINDOW_BUFFER(mini_buffer_window());

    if (mini_buffer_completion_cache.update(buffer->changes.len)) {
        buffer->contents.stringify_into(cz::heap_allocator(),
                                        &mini_buffer_completion_cache.engine_context.query);
    }
}

void Client::show_dialog(Dialog dialog) {
    dealloc_message();

    {
        // Setup the mini buffer's contents.
        Window_Unified* window = mini_buffer_window();
        WITH_WINDOW_BUFFER(window);

        Transaction transaction;
        transaction.init(buffer);
        CZ_DEFER(transaction.drop());

        if (buffer->contents.len > 0) {
            Edit edit;
            edit.value = buffer->contents.slice(transaction.value_allocator(),
                                                buffer->contents.start(), buffer->contents.len);
            edit.position = 0;
            edit.flags = Edit::REMOVE;
            transaction.push(edit);
        }

        if (dialog.mini_buffer_contents.len > 0) {
            Edit edit;
            edit.value =
                SSOStr::as_duplicate(transaction.value_allocator(), dialog.mini_buffer_contents);
            edit.position = 0;
            edit.flags = Edit::INSERT;
            transaction.push(edit);
        }

        transaction.commit(this);

        // Set the tokenizer.
        Tokenizer tokenizer = dialog.next_token ? dialog.next_token : default_next_token;
        buffer->set_tokenizer(tokenizer);
    }

    show_message(dialog.prompt);
    _message.tag = Message::RESPOND;
    _message.completion_engine = dialog.completion_engine;
    _message.response_callback = dialog.response_callback;
    _message.interactive_response_callback = dialog.interactive_response_callback;
    _message.response_cancel = dialog.response_cancel;
    _message.response_callback_data = dialog.response_callback_data;
    _select_mini_buffer = true;

    setup_completion_cache(this);
}

}
