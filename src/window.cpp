#include "window.hpp"

#include <stdlib.h>
#include <Tracy.hpp>
#include <algorithm>
#include <cz/heap.hpp>
#include "buffer.hpp"
#include "change.hpp"
#include "client.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "movement.hpp"
#include "token.hpp"
#include "visible_region.hpp"

namespace mag {

Window_Unified* Window_Unified::create(cz::Arc<Buffer_Handle> buffer_handle) {
    Window_Unified* window = cz::heap_allocator().alloc<Window_Unified>();
    CZ_ASSERT(window);
    window->parent = nullptr;
    window->total_rows = 0;
    window->total_cols = 0;
    window->tag = Window::UNIFIED;

    window->buffer_handle = buffer_handle.clone();
    window->start_position = 0;
    window->column_offset = 0;

    window->change_index = 0;
    window->cursors = {};
    window->cursors.reserve(cz::heap_allocator(), 1);
    window->cursors.push({});
    window->selected_cursor = 0;
    window->show_marks = false;

    window->completion_cache = {};  // not sure if this is really necessary
    window->completion_cache.init();
    window->completing = false;

    window->pinned = false;
    return window;
}

Window_Unified* Window_Unified::clone() {
    Window_Unified* window = cz::heap_allocator().alloc<Window_Unified>();
    CZ_ASSERT(window);
    *window = *this;
    window->cursors = this->cursors.clone(cz::heap_allocator());
    window->selected_cursor = this->selected_cursor;
    window->completion_cache = {};
    window->completion_cache.init();
    window->completing = false;
    window->buffer_handle = buffer_handle.clone();
    return window;
}

void Window_Unified::update_cursors(const Buffer* buffer) {
    ZoneScoped;

    cz::Slice<const Change> new_changes = buffer->changes.slice_start(change_index);

    if (new_changes.len == 0) {
        return;
    }

    cz::Slice<Cursor> cursors = this->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        position_after_changes(new_changes, &cursors[c].point);
        position_after_changes(new_changes, &cursors[c].mark);
    }

    bool was_0 = start_position == 0;
    position_after_changes(new_changes, &start_position);
    if (was_0) {
        // The only case where we can insert before the start position is if we are at the start of
        // the file.  This hack will cause the algorithm in `render/render.cpp:draw_buffer_contents`
        // (commit 5374379) to refit the start position to the cursor if the changes are larger than
        // one screen height.  This fixes the bug where opening a new file and pasting will make it
        // appear that the file is empty because `start_position` is updated to the cursor's
        // position instead of being at the top of the file.
        start_position = 0;
    }

    this->change_index = buffer->changes.len();
}

void Window_Unified::start_completion(Completion_Engine completion_engine) {
    completion_cache.set_engine(completion_engine);
    completing = true;
}

void Window_Unified::update_completion_cache(const Buffer* buffer) {
    CZ_DEBUG_ASSERT(completing);

    if (completion_cache.update(buffer->changes.len())) {
        Contents_Iterator iterator = buffer->contents.iterator_at(cursors[selected_cursor].point);
        Token token;
        if (!get_token_at_position_no_update(buffer, &iterator, &token)) {
            abort_completion();
            return;
        }

        completion_cache.engine_context.query.reserve(cz::heap_allocator(),
                                                      token.end - token.start);
        buffer->contents.slice_into(iterator, token.end, &completion_cache.engine_context.query);
    }
}

void Window_Unified::finish_completion(Client* client, Buffer* buffer) {
    CZ_DEBUG_ASSERT(completing);
    completing = false;

    Completion_Filter_Context* context = &completion_cache.filter_context;
    if (context->selected >= context->results.len()) {
        return;
    }

    // TODO: multi cursors?
    Contents_Iterator iterator = buffer->contents.iterator_at(cursors[selected_cursor].point);
    Token token;
    bool do_remove = get_token_at_position(buffer, &iterator, &token);

    cz::Str value = context->results[context->selected];

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    if (do_remove) {
        Edit remove;
        remove.value = buffer->contents.slice(transaction.value_allocator(), iterator, token.end);
        remove.position = token.start;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);
    }

    Edit insert;
    insert.value = SSOStr::as_duplicate(transaction.value_allocator(), value);
    insert.position = token.start;
    insert.flags = Edit::INSERT;
    transaction.push(insert);

    transaction.commit(client);
}

void Window_Unified::abort_completion() {
    completing = false;
}

void Window::drop_(Window* window) {
    switch (window->tag) {
    case UNIFIED: {
        Window_Unified* w = (Window_Unified*)window;
        w->cursors.drop(cz::heap_allocator());
        w->completion_cache.drop();
        w->buffer_handle.drop();
        cz::heap_allocator().dealloc(w);
        break;
    }

    case VERTICAL_SPLIT:
    case HORIZONTAL_SPLIT: {
        Window_Split* w = (Window_Split*)window;
        Window::drop_(w->first);
        Window::drop_(w->second);
        cz::heap_allocator().dealloc(w);
        break;
    }
    }
}

Window_Split* Window_Split::create(Window::Tag tag, Window* first, Window* second) {
    Window_Split* window = cz::heap_allocator().alloc<Window_Split>();
    CZ_ASSERT(window);
    window->parent = nullptr;
    window->tag = tag;

    window->first = first;
    window->second = second;
    window->split_ratio = 0.5f;
    window->fused = false;
    return window;
}

void Window::set_size(size_t _total_rows, size_t _total_cols) {
    CZ_ASSERT(_total_rows >= 1);
    CZ_ASSERT(_total_cols >= 1);

    total_rows = _total_rows;
    total_cols = _total_cols;

    // No children to update.
    if (tag == Window::UNIFIED) {
        return;
    }

    Window_Split* split = (Window_Split*)this;
    split->set_children_size();
}

void Window_Split::set_children_size() {
    if (tag == Window::VERTICAL_SPLIT) {
        CZ_ASSERT(total_cols >= 1);

        // Lose a column due to the separator (|).
        size_t avail_cols = total_cols - 1;
        size_t left_cols = avail_cols * split_ratio;
        size_t right_cols = avail_cols - left_cols;

        first->set_size(total_rows, left_cols);
        second->set_size(total_rows, right_cols);
    } else {
        size_t avail_rows = total_rows;
        size_t top_rows = avail_rows * split_ratio;
        size_t bottom_rows = avail_rows - top_rows;

        first->set_size(top_rows, total_cols);
        second->set_size(bottom_rows, total_cols);
    }
}

void Window_Split::drop_non_recursive(Window_Split* window) {
    cz::heap_allocator().dealloc(window);
}

void kill_extra_cursors(Window_Unified* window, Client* client) {
    // :CopyLeak we don't deallocate here
    Cursor cursor = window->cursors[window->selected_cursor];
    window->cursors.set_len(1);
    window->cursors[0] = cursor;
    window->selected_cursor = 0;

    if (window->cursors[0].local_copy_chain) {
        // Put the local copy chain on top of the global one.
        Copy_Chain* copy_chain = window->cursors[0].local_copy_chain;
        while (copy_chain->previous) {
            copy_chain = copy_chain->previous;
        }
        copy_chain->previous = client->global_copy_chain;

        // Set the global copy chain to the top.
        client->global_copy_chain = window->cursors[0].local_copy_chain;
        window->cursors[0].local_copy_chain = nullptr;
    }
}

Contents_Iterator nearest_character(const Window_Unified* window,
                                    const Buffer* buffer,
                                    uint32_t row,
                                    uint32_t column) {
    Contents_Iterator iterator = buffer->contents.iterator_at(window->start_position);

    row = std::min(row, (uint32_t)window->rows() - 1);
    column = std::min(column, (uint32_t)window->cols() - 1);

    uint32_t it_row = 0;
    uint32_t it_column = 0;
    if (it_row != row) {
        // Find the visual row that matches.
        while (!iterator.at_eob()) {
            char ch = iterator.get();
            ++it_column;
            iterator.advance();

            // Every character is rendered at one width.  We wrap at the
            // window width.  Newlines wrap after they are rendered.
            if (it_column == window->cols() || ch == '\n') {
                ++it_row;
                it_column = 0;
                if (it_row == row) {
                    break;
                }
            }
        }
    }

    // Find the visual column that matches.
    while (!iterator.at_eob()) {
        if (it_column == column) {
            break;
        }

        // End of the line so this is the best guess.
        if (iterator.get() == '\n') {
            break;
        }

        iterator.advance();
        ++it_column;
    }

    return iterator;
}

Window_Unified* window_first(Window* window) {
    switch (window->tag) {
    case Window::UNIFIED:
        return (Window_Unified*)window;

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT:
        return window_first(((Window_Split*)window)->first);
    }

    CZ_PANIC("");
}

Window_Unified* window_last(Window* window) {
    switch (window->tag) {
    case Window::UNIFIED:
        return (Window_Unified*)window;

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT:
        return window_last(((Window_Split*)window)->second);
    }

    CZ_PANIC("");
}

void toggle_cycle_window(Client* client) {
    Window* window = client->selected_normal_window;
    if (!window->parent) {
        return;
    }

    if (window == window->parent->first) {
        client->selected_normal_window = window_first(window->parent->second);
    } else {
        client->selected_normal_window = window_last(window->parent->first);
    }
}

void cycle_window(Client* client) {
    Window* child = client->selected_normal_window;
    Window_Split* parent = child->parent;
    while (parent) {
        if (parent->first == child) {
            client->selected_normal_window = window_first(parent->second);
            return;
        }

        child = parent;
        parent = child->parent;
    };

    client->selected_normal_window = window_first(client->window);
}

void reverse_cycle_window(Client* client) {
    Window* child = client->selected_normal_window;
    Window_Split* parent = child->parent;
    while (parent) {
        if (parent->second == child) {
            client->selected_normal_window = window_last(parent->first);
            return;
        }

        child = parent;
        parent = child->parent;
    };

    client->selected_normal_window = window_last(client->window);
}

Window_Split* split_window(Client* client, Window::Tag tag) {
    Window* top = client->selected_normal_window;
    Window_Unified* bottom = client->selected_normal_window->clone();

    if (top->parent && top->parent->fused) {
        top = top->parent;
    }

    Window_Split* parent = Window_Split::create(tag, top, bottom);

    client->replace_window(top, parent);

    top->parent = parent;
    bottom->parent = parent;

    client->selected_normal_window = bottom;

    return parent;
}

}
