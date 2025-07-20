#pragma once

#include <stddef.h>
#include <stdint.h>
#include <cz/arc.hpp>
#include <cz/vector.hpp>
#include "core/completion.hpp"
#include "core/cursor.hpp"

namespace mag {
struct Buffer;
struct Buffer_Handle;
struct Contents_Iterator;
struct Client;
struct Contents;
struct Theme;

struct Window_Unified;
struct Window_Split;

struct Window {
    Window_Split* parent;

    size_t total_rows;
    size_t total_cols;

    enum Tag {
        UNIFIED,
        VERTICAL_SPLIT,
        HORIZONTAL_SPLIT,
    } tag;

    static void drop_(Window* window);

    /// Set the size of this window and its children if it has any.
    void set_size(size_t total_rows, size_t total_cols);

    Window_Unified* find(uint64_t id) const;
};

struct Window_Unified : Window {
    /// Incrementing ID; reset when a new buffer is assigned to this window.
    uint64_t id;

    /// The buffer this window is associated with.  If the buffer (which is owned by
    /// the `Editor`) is killed then this window needs to be replaced by the killer.
    cz::Arc<Buffer_Handle> buffer_handle;

    uint64_t start_position;
    uint64_t column_offset;

    size_t change_index;
    cz::Vector<Cursor> cursors;
    size_t selected_cursor;

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

    /// Clones the `Buffer_Handle`.
    static Window_Unified* create(cz::Arc<Buffer_Handle> buffer_handle, uint64_t id);
    Window_Unified* clone(uint64_t new_id);

    void update_cursors(const Buffer* buffer, Client* client);

    void start_completion(Completion_Engine completion_engine);

    /// Update the completion cache for the buffer.  Note that you must call
    /// `buffer->token_cache.update(buffer)` before invoking this function or
    /// else the results will be undefined.  It would call it itself but it needs
    /// to not modify the buffer so we can render immutable buffer objects.
    void update_completion_cache(const Buffer*);

    void finish_completion(Client* client, Buffer*);
    void abort_completion();

    void clear_show_marks_temporarily() {
        if (show_marks == 2) {
            show_marks = false;
        }
    }

    size_t rows() const { return total_rows - 1; }

    /// Get the selected cursor.
    Cursor& sel() { return cursors[selected_cursor]; }
    const Cursor& sel() const { return cursors[selected_cursor]; }
};

size_t line_number_cols(const Theme& theme, const Window_Unified* window, const Buffer* buffer);
size_t line_number_cols(const Theme& theme, const Window_Unified* window, const Contents* contents);

struct Window_Split : Window {
    Window* first;
    Window* second;

    /// Percent of `rows` allocated to the `first` child.  `0.6` means 60%.
    float split_ratio;

    /// If a split is fused then the children & structure shouldn't be
    /// changed.  This is used to fuse the search window with its results.
    ///
    /// Commands such as `command_split_window_horizontal` will walk up the window
    /// tree before splitting so as to keep these two windows under the same parent.
    bool fused;

    static Window_Split* create(Window::Tag tag, Window* first, Window* second);
    static void drop_non_recursive(Window_Split* window);

    /// Set the size of the children of this window based on the size of this window.
    void set_children_size();
};

struct Client;
void kill_extra_cursors(Window_Unified* window, Client* client);
void kill_cursor(Window_Unified* window, Client* client, size_t index);
void kill_cursors_at_same_point(Window_Unified* window, Client* client);

Contents_Iterator nearest_character(const Window_Unified* window,
                                    const Buffer* buffer,
                                    const Theme& theme,
                                    uint32_t row,
                                    uint32_t column);

Window_Unified* window_first(Window* window);
Window_Unified* window_last(Window* window);

/// Toggle between the children of the selected window's split.
void toggle_cycle_window(Client* client);

/// Go to the next window in a clockwise rotation / depth first traversal.
void cycle_window(Client* client);
void reverse_cycle_window(Client* client);

Window_Split* split_window(Client* client, Window::Tag tag);

}
