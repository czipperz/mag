#pragma once

#include <stddef.h>
#include <stdint.h>
#include <cz/vector.hpp>
#include "buffer_id.hpp"
#include "completion.hpp"
#include "cursor.hpp"

namespace mag {
struct Buffer;
struct Contents_Iterator;

struct Window_Split;

struct Window {
    Window_Split* parent;

    size_t rows;
    size_t cols;

    enum Tag {
        UNIFIED,
        VERTICAL_SPLIT,
        HORIZONTAL_SPLIT,
    } tag;

    static void drop_(Window* window);
};

struct Window_Unified : Window {
    Buffer_Id id;
    uint64_t start_position;

    size_t change_index;
    cz::Vector<Cursor> cursors;

    /// `show_marks` can be set to `false`, `true`, or `2`, meaning Notepad mode.
    ///
    /// In Notepad, instead of explicitly starting to select text (ie `command_set_mark`), you
    /// implicitly start a selection by adding a shift modifier to keys (ex `S-RIGHT`).  The first
    /// non-shift-modified key clears the selection.  If this is `true` then we are emulating that
    /// behavior; commands should check this by calling `clear_show_marks_temporarily`.
    ///
    /// Committing to the `Buffer` will clear this automatically.
    uint8_t show_marks;

    Completion_Cache completion_cache;
    bool completing;

    /// If a window is pinned then it won't be closed via `command_one_window_except_pinned`.
    bool pinned;

    static Window_Unified* create(Buffer_Id buffer_id);
    Window_Unified* clone();

    void update_cursors(const Buffer* buffer);

    void start_completion(Completion_Engine completion_engine);

    /// Update the completion cache for the buffer.  Note that you must call
    /// `buffer->token_cache.update(buffer)` before invoking this function or
    /// else the results will be undefined.  It would call it itself but it needs
    /// to not modify the buffer so we can render immutable buffer objects.
    void update_completion_cache(const Buffer*);

    void finish_completion(Buffer*);
    void abort_completion();

    void clear_show_marks_temporarily() {
        if (show_marks == 2) {
            show_marks = false;
        }
    }
};

struct Window_Split : Window {
    Window* first;
    Window* second;

    /// Percent of `rows` allocated to the `first` child.  `0.6` means 60%.
    float split_ratio;

    static Window_Split* create(Window::Tag tag, Window* first, Window* second);
    static void drop_non_recursive(Window_Split* window);
};

struct Client;
void kill_extra_cursors(Window_Unified* window, Client* client);

Contents_Iterator nearest_character(const Window_Unified* window,
                                    const Buffer* buffer,
                                    uint32_t row,
                                    uint32_t column);

}
