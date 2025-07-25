#pragma once

#include <stddef.h>
#include <stdint.h>
#include <chrono>
#include <cz/vector.hpp>
#include "core/buffer.hpp"
#include "core/editor.hpp"
#include "core/window.hpp"

namespace mag {
namespace render {

struct Window_Unified_Cache {
    uint64_t window_id;
    size_t change_index;
    uint64_t visible_start;
    size_t cursor_count;
    uint64_t selected_cursor_mark;

    // Animate when the visible region shifts.
    struct Animated_Scrolling {
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
        uint64_t start_line;
        uint64_t end_line;
        uint64_t start_position;
        uint64_t end_position;
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
