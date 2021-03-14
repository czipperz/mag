#pragma once

#include <stddef.h>
#include <stdint.h>
#include <cz/vector.hpp>
#include "buffer.hpp"
#include "editor.hpp"
#include "window.hpp"

namespace mag {
namespace client {

struct Window_Unified_Cache {
    Buffer_Id id;
    size_t change_index;
    uint64_t visible_start;
    uint64_t visible_end;

    // Animate when the visible region shifts.
    struct {
        uint64_t max_offset;
        int64_t line_offset;
        float speed;

        // For performance reasons, cache the visible region position of the last frame.
        bool has_cache;
        size_t cached_change_index;
        int64_t cached_line_offset;
        Contents_Iterator cached_iterator;
    } animation;
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
bool cache_windows_check_points(Window_Cache* window_cache,
                                Window* w,
                                Editor* editor,
                                bool (*callback)(void*),
                                void* callback_data);
void cache_window_unified_position(Window_Unified* window,
                                   Window_Cache* window_cache,
                                   uint64_t start_position,
                                   Buffer* buffer);
void cache_window_unified_create(Editor* editor,
                                 Window_Cache* window_cache,
                                 Window_Unified* window);

}
}
