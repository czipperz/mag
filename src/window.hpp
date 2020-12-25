#pragma once

#include <stddef.h>
#include <cz/vector.hpp>
#include "buffer_id.hpp"
#include "cursor.hpp"
#include "completion.hpp"

namespace mag {
struct Buffer;

struct Change;
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
    bool show_marks;

    Completion_Cache completion_cache;
    bool completing;

    /// If a window is pinned then it won't be closed via `command_one_window_except_pinned`.
    bool pinned;

    static Window_Unified* create(Buffer_Id buffer_id);
    Window_Unified* clone();

    void update_cursors(Buffer* buffer);

    void start_completion(Completion_Engine completion_engine);
    void update_completion_cache(Buffer*);
    void finish_completion(Buffer*);
    void abort_completion();
};

struct Window_Split : Window {
    Window* first;
    Window* second;

    static Window_Split* create(Window::Tag tag, Window* first, Window* second);
    static void drop_non_recursive(Window_Split* window);
};

struct Client;
void kill_extra_cursors(Window_Unified* window, Client* client);

}
