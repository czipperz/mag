#include "window_cache.hpp"

#include "command_macros.hpp"
#include "token.hpp"
#include "visible_region.hpp"

namespace mag {
namespace render {

void destroy_window_cache_children(Window_Cache* window_cache) {
    switch (window_cache->tag) {
    case Window::UNIFIED:
        break;
    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT:
        destroy_window_cache(window_cache->v.split.first);
        destroy_window_cache(window_cache->v.split.second);
        break;
    }
}

void destroy_window_cache(Window_Cache* window_cache) {
    if (!window_cache) {
        return;
    }

    destroy_window_cache_children(window_cache);

    cz::heap_allocator().dealloc(window_cache);
}

void cache_window_unified_position(Window_Unified* window,
                                   Window_Cache* window_cache,
                                   uint64_t start_position,
                                   const Buffer* buffer) {
    ZoneScoped;

    window_cache->v.unified.visible_start = start_position;
    window->start_position = start_position;
    window_cache->v.unified.change_index = buffer->changes.len();
    window_cache->v.unified.selected_cursor_mark = window->cursors[window->selected_cursor].mark;
}

void cache_window_unified_create(Editor* editor,
                                 Window_Cache* window_cache,
                                 Window_Unified* window,
                                 const Buffer* buffer) {
    ZoneScoped;

    window_cache->tag = Window::UNIFIED;
    window_cache->v.unified = {};
    window_cache->v.unified.id = buffer->id;
    window_cache->v.unified.cursor_count = window->cursors.len();
    window_cache->v.unified.animated_scrolling.visible_start = window->start_position;
    cache_window_unified_position(window, window_cache, window->start_position, buffer);
}

}
}
