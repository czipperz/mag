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

Window_Unified* Window_Unified::create(Buffer_Id buffer_id) {
    Window_Unified* window = cz::heap_allocator().alloc<Window_Unified>();
    CZ_ASSERT(window);
    window->parent = nullptr;
    window->rows = 0;
    window->cols = 0;
    window->tag = Window::UNIFIED;

    window->id = buffer_id;
    window->start_position = 0;

    window->change_index = 0;
    window->cursors = {};
    window->cursors.reserve(cz::heap_allocator(), 1);
    window->cursors.push({});
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
    window->completion_cache = {};
    window->completion_cache.init();
    window->completing = false;
    return window;
}

void Window_Unified::update_cursors(const Buffer* buffer) {
    ZoneScoped;

    cz::Slice<Cursor> cursors = this->cursors;
    cz::Slice<const Change> new_changes = {buffer->changes.elems() + change_index,
                                           buffer->changes.len() - change_index};
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
        Contents_Iterator iterator = buffer->contents.iterator_at(cursors[0].point);
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

void Window_Unified::finish_completion(Buffer* buffer) {
    CZ_DEBUG_ASSERT(completing);
    completing = false;

    Completion_Filter_Context* context = &completion_cache.filter_context;
    if (context->selected >= context->results.len()) {
        return;
    }

    // Todo: multi cursors?
    Contents_Iterator iterator = buffer->contents.iterator_at(cursors[0].point);
    Token token;
    bool do_remove = get_token_at_position(buffer, &iterator, &token);

    cz::Str value = context->results[context->selected];

    Transaction transaction;
    transaction.init(do_remove + 1, (do_remove ? token.end - token.start : 0) + value.len);
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

    transaction.commit(buffer);
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
    window->rows = first->rows + second->rows + 1;
    window->cols = first->cols + second->cols + 1;
    window->tag = tag;

    window->first = first;
    window->second = second;
    window->split_ratio = 0.5f;
    return window;
}

void Window_Split::drop_non_recursive(Window_Split* window) {
    cz::heap_allocator().dealloc(window);
}

void kill_extra_cursors(Window_Unified* window, Client* client) {
    // :CopyLeak we don't deallocate here
    window->cursors.set_len(1);
    Copy_Chain* copy_chain = window->cursors[0].local_copy_chain;
    if (copy_chain) {
        while (copy_chain->previous) {
            copy_chain = copy_chain->previous;
        }
        copy_chain->previous = client->global_copy_chain;
        client->global_copy_chain = copy_chain;
        window->cursors[0].local_copy_chain = nullptr;
    }
}

Contents_Iterator nearest_character(const Window_Unified* window,
                                    const Buffer* buffer,
                                    uint32_t row,
                                    uint32_t column) {
    Contents_Iterator iterator = buffer->contents.iterator_at(window->start_position);

    row = std::min(row, (uint32_t)window->rows - 1);
    column = std::min(column, (uint32_t)window->cols - 1);

    uint32_t it_row = 0;
    uint32_t it_column = 0;
    if (it_row == row) {
        goto match_column;
    }

    // Find the visual row that matches.
    while (!iterator.at_eob()) {
        char ch = iterator.get();
        ++it_column;
        iterator.advance();

        // Every character is rendered at one width.
        if (it_column == window->cols + 1) {
            ++it_row;
            it_column = 0;
            if (it_row == row) {
                goto match_column;
            }
        }

        // Then newlines wrap after they are rendered.
        if (ch == '\n') {
            ++it_row;
            it_column = 0;
            if (it_row == row) {
                goto match_column;
            }
        }
    }

match_column:
    // Find the visual column that matches.
    while (!iterator.at_eob()) {
        ++it_column;

        if (it_column == column) {
            break;
        }

        // End of the line so this is the best guess.
        if (iterator.get() == '\n') {
            break;
        }

        iterator.advance();
    }

    return iterator;
}

}
