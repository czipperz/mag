#include "window_cache.hpp"

#include <cz/bit_array.hpp>
#include "command_macros.hpp"
#include "token.hpp"
#include "visible_region.hpp"

namespace mag {
namespace client {

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

bool cache_windows_check_points(Window_Cache* window_cache,
                                Window* w,
                                Editor* editor,
                                bool (*callback)(void*),
                                void* callback_data) {
    ZoneScoped;

    CZ_DEBUG_ASSERT(window_cache->tag == w->tag);

    switch (w->tag) {
    case Window::UNIFIED: {
        Window_Unified* window = (Window_Unified*)w;

        // TODO: Make this non blocking!
        {
            WITH_WINDOW_BUFFER(window);
            buffer->token_cache.update(buffer);

            if (buffer->token_cache.ran_to_end) {
                return false;
            }

            uint64_t state;
            Contents_Iterator iterator;
            if (buffer->token_cache.check_points.len() > 0) {
                state = buffer->token_cache.check_points.last().state;
                iterator =
                    buffer->contents.iterator_at(buffer->token_cache.check_points.last().position);
            } else {
                state = 0;
                iterator = buffer->contents.start();
            }

            for (size_t i = 0; i < 100; ++i) {
                if (callback(callback_data)) {
                    return true;
                }

                if (!buffer->token_cache.next_check_point(buffer, &iterator, &state)) {
                    return false;
                }
            }

            return true;
        }
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)w;
        return cache_windows_check_points(window_cache->v.split.first, window->first, editor,
                                          callback, callback_data) ||
               cache_windows_check_points(window_cache->v.split.second, window->second, editor,
                                          callback, callback_data);
    }
    }

    CZ_PANIC("");
}

void cache_window_unified_position(Window_Unified* window,
                                   Window_Cache* window_cache,
                                   uint64_t start_position,
                                   Buffer* buffer) {
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
