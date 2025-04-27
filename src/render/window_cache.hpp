#pragma once

#include <stddef.h>
#include <stdint.h>
#include <cz/vector.hpp>
#include "buffer.hpp"
#include "editor.hpp"
#include "window.hpp"

namespace mag {
namespace render {

struct Window_Unified_Cache {
    Buffer_Id id;
    size_t change_index;
    uint64_t visible_start;
    size_t cursor_count;
    uint64_t selected_cursor_mark;

    // Animate when the visible region shifts.
    struct {
        float speed;
        uint64_t visible_start;
        bool slam_on_the_breaks;
    } animated_scrolling;
};

struct Window_Cache {
    Window::Tag tag;
    union {
        Window_Unified_Cache unified;
        struct {
            Window_Cache* first;
            Window_Cache* second;
        } split;
    } v;
};

void destroy_window_cache_children(Window_Cache* window_cache);
void destroy_window_cache(Window_Cache* window_cache);
void cache_window_unified_position(Window_Unified* window,
                                   Window_Cache* window_cache,
                                   uint64_t start_position,
                                   const Buffer* buffer);
void cache_window_unified_create(Editor* editor,
                                 Window_Cache* window_cache,
                                 Window_Unified* window,
                                 const Buffer* buffer);

}
}
