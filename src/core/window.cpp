#include "window.hpp"

#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <cz/heap.hpp>
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/change.hpp"
#include "core/client.hpp"
#include "core/command_macros.hpp"
#include "core/editor.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"
#include "core/visible_region.hpp"

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

    clear_show_marks_temporarily();

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

    this->change_index = buffer->changes.len;
}

void Window_Unified::start_completion(Completion_Engine completion_engine) {
    completion_cache.set_engine(completion_engine);
    completing = true;
}

void Window_Unified::update_completion_cache(const Buffer* buffer) {
    CZ_DEBUG_ASSERT(completing);

    if (completion_cache.update(buffer->changes.len)) {
        Contents_Iterator iterator = buffer->contents.iterator_at(cursors[selected_cursor].point);
        Contents_Iterator middle = iterator;
        Token token;
        if (!get_token_at_position_no_update(buffer, &iterator, &token)) {
            abort_completion();
            return;
        }

        completion_cache.engine_context.query.reserve(cz::heap_allocator(),
                                                      middle.position - iterator.position);
        buffer->contents.slice_into(iterator, middle.position,
                                    &completion_cache.engine_context.query);
    }
}

void Window_Unified::finish_completion(Client* client, Buffer* buffer) {
    CZ_DEBUG_ASSERT(completing);
    completing = false;

    Completion_Filter_Context* context = &completion_cache.filter_context;
    if (context->selected >= context->results.len) {
        return;
    }

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Str completion_result_str = context->results[context->selected];
    SSOStr completion_result =
        SSOStr::as_duplicate(transaction.value_allocator(), completion_result_str);
    int64_t offset = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator iterator = buffer->contents.iterator_at(cursors[c].point);
        uint64_t insertion_position = iterator.position;

        Edit insert;
        insert.value = completion_result;

        Contents_Iterator token_start = iterator;
        Token token;
        bool found_token = get_token_at_position(buffer, &token_start, &token);
        bool do_remove = found_token;
        if (found_token && iterator.position >= token.start &&
            iterator.position - token.start <= completion_result_str.len &&
            matches(token_start, iterator.position,
                    completion_result_str.slice_end(iterator.position - token.start))) {
            // If the cursor is inside/at a token that matches the
            // completion result, then just insert the new suffix.
            do_remove = false;
            insert.value = SSOStr::from_constant(
                completion_result.as_str().slice_start(iterator.position - token.start));
        }

        int64_t pending_offset = 0;
        if (do_remove) {
            Edit remove;
            remove.value =
                buffer->contents.slice(transaction.value_allocator(), token_start, token.end);
            remove.position = token.start + offset;
            remove.flags = Edit::REMOVE;
            transaction.push(remove);
            pending_offset -= token.end - token_start.position;
            insertion_position = token.start;
        }

        insert.position = insertion_position + offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
        offset += pending_offset + insert.value.len();
    }

    transaction.commit(client);
}

void Window_Unified::abort_completion() {
    completing = false;
}

size_t line_number_cols(const Theme& theme, const Window_Unified* window, const Buffer* buffer) {
    return line_number_cols(theme, window, &buffer->contents);
}
size_t line_number_cols(const Theme& theme,
                        const Window_Unified* window,
                        const Contents* contents) {
    if (!theme.draw_line_numbers)
        return 0;

    size_t end_line_number = contents->get_line_number(contents->len);
    size_t line_number_width = (size_t)log10(end_line_number) + 1;
    size_t result = line_number_width + 1 /* space on right */;

    // Enable drawing line numbers for non-mini buffer
    // windows if they are enabled and fit on the screen.
    if (result + 5 > window->total_cols)
        return 0;
    return result;
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
        size_t left_cols = (size_t)(avail_cols * split_ratio);
        size_t right_cols = avail_cols - left_cols;

        first->set_size(total_rows, left_cols);
        second->set_size(total_rows, right_cols);
    } else {
        size_t avail_rows = total_rows;
        size_t top_rows = (size_t)(avail_rows * split_ratio);
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
    window->cursors.len = 1;
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

        client->set_system_clipboard(client->global_copy_chain->value.as_str());
    }
}

void kill_cursor(Window_Unified* window, Client* client, size_t index) {
    CZ_DEBUG_ASSERT(index < window->cursors.len);

    // When going down to 1 cursor we need to call `kill_extra_cursors` to cleanup various settings.
    if (window->cursors.len == 2) {
        if (window->selected_cursor == index)
            window->selected_cursor = 1 - window->selected_cursor;
        kill_extra_cursors(window, client);
        return;
    }

    window->cursors.remove(index);

    // Make sure the selected cursor is still in bounds.  Prefer moving forwards.
    if (window->selected_cursor > index || window->selected_cursor == window->cursors.len) {
        --window->selected_cursor;
    }
}

Contents_Iterator nearest_character(const Window_Unified* window,
                                    const Buffer* buffer,
                                    const Theme& theme,
                                    uint32_t row,
                                    uint32_t column) {
    Contents_Iterator iterator = buffer->contents.iterator_at(window->start_position);

    row = std::min(row, (uint32_t)window->rows() - 1);
    column = std::min(column, (uint32_t)window->total_cols - 1);

    forward_visual_line(window, buffer->mode, theme, &iterator, row);

    if (buffer->mode.wrap_long_lines) {
        uint64_t line_column = get_visual_column(buffer->mode, iterator);
        go_to_visual_column(buffer->mode, &iterator, line_column + column);
    } else {
        uint64_t line_column = window->column_offset;
        go_to_visual_column(buffer->mode, &iterator, line_column + column);
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
