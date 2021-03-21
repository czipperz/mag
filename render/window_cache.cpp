#include "window_cache.hpp"

#include <cz/bit_array.hpp>
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

    free(window_cache);
}

void cache_window_unified_position(Window_Unified* window,
                                   Window_Cache* window_cache,
                                   uint64_t start_position,
                                   const Buffer* buffer) {
    ZoneScoped;

    Contents_Iterator visible_end_iterator = buffer->contents.iterator_at(start_position);
    compute_visible_end(window, &visible_end_iterator);
    window_cache->v.unified.visible_start = start_position;
    window->start_position = start_position;
    window_cache->v.unified.visible_end = visible_end_iterator.position;
    window_cache->v.unified.change_index = buffer->changes.len();
}

void cache_window_unified_create(Editor* editor,
                                 Window_Cache* window_cache,
                                 Window_Unified* window) {
    ZoneScoped;

    WITH_WINDOW_BUFFER(window);
    window_cache->tag = Window::UNIFIED;
    window_cache->v.unified.id = window->id;
    window_cache->v.unified.animation = {};
    window_cache->v.unified.animation.visible_start = window->start_position;
    cache_window_unified_position(window, window_cache, window->start_position, buffer);
}

}
}
