#include "window_cache.hpp"

#include <ncurses.h>
#include <cz/bit_array.hpp>
#include "command_macros.hpp"
#include "token.hpp"
#include "visible_region.hpp"

namespace mag {
namespace client {

void destroy_window_cache_children(Window_Cache* window_cache) {
    switch (window_cache->tag) {
    case Window::UNIFIED:
        window_cache->v.unified.tokenizer_check_points.drop(cz::heap_allocator());
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

int cache_windows_check_points(Window_Cache* window_cache, Window* w, Editor* editor) {
    ZoneScoped;

    CZ_DEBUG_ASSERT(window_cache->tag == w->tag);

    switch (w->tag) {
    case Window::UNIFIED: {
        Window_Unified* window = (Window_Unified*)w;

        if (window_cache->v.unified.tokenizer_ran_to_end) {
            return ERR;
        }

        // TODO: Make this non blocking!
        {
            WITH_WINDOW_BUFFER(window);
            cz::Vector<Tokenizer_Check_Point>* check_points =
                &window_cache->v.unified.tokenizer_check_points;

            uint64_t state;
            Contents_Iterator iterator;
            if (check_points->len() > 0) {
                state = check_points->last().state;
                iterator = buffer->contents.iterator_at(check_points->last().position);
            } else {
                state = 0;
                iterator = buffer->contents.iterator_at(0);
            }

            while (1) {
                int getch_result = getch();
                if (getch_result != ERR) {
                    return getch_result;
                }

                if (!next_check_point(window_cache, buffer, &iterator, &state, check_points)) {
                    window_cache->v.unified.tokenizer_ran_to_end = true;
                    return ERR;
                }
            }
        }
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)w;
        int first_result =
            cache_windows_check_points(window_cache->v.split.first, window->first, editor);
        if (first_result != ERR) {
            return first_result;
        }
        return cache_windows_check_points(window_cache->v.split.second, window->second, editor);
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
    window_cache->v.unified.visible_end = visible_end_iterator.position;

    cz::Vector<Tokenizer_Check_Point>* check_points =
        &window_cache->v.unified.tokenizer_check_points;

    uint64_t state;
    Contents_Iterator iterator;
    if (check_points->len() > 0) {
        state = check_points->last().state;
        iterator = buffer->contents.iterator_at(check_points->last().position);
    } else {
        state = 0;
        iterator = buffer->contents.iterator_at(0);
    }

    while (iterator.position <= start_position) {
        if (!next_check_point(window_cache, buffer, &iterator, &state, check_points)) {
            break;
        }
    }
}

void cache_window_unified_update(Window_Cache* window_cache,
                                 Window_Unified* window,
                                 Buffer* buffer) {
    ZoneScoped;

    cz::Slice<Change> changes = buffer->changes;
    cz::Slice<Tokenizer_Check_Point> check_points = window_cache->v.unified.tokenizer_check_points;
    unsigned char* changed_check_points =
        (unsigned char*)calloc(1, cz::bit_array::alloc_size(check_points.len));
    CZ_DEFER(free(changed_check_points));
    // Detect check points that changed
    for (size_t i = 0; i < check_points.len; ++i) {
        uint64_t pos = check_points[i].position;

        position_after_changes({changes.elems + window_cache->v.unified.change_index,
                                changes.len - window_cache->v.unified.change_index},
                               &pos);

        if (check_points[i].position != pos) {
            cz::bit_array::set(changed_check_points, i);
        }

        check_points[i].position = pos;
    }
    window_cache->v.unified.change_index = changes.len;

    Token token;
    token.end = 0;
    uint64_t state = 0;
    // Fix check points that were changed
    for (size_t i = 0; i < check_points.len; ++i) {
        uint64_t end_position = check_points[i].position;
        if (cz::bit_array::get(changed_check_points, i)) {
            Contents_Iterator iterator = buffer->contents.iterator_at(token.end);
            // Efficiently loop without recalculating the iterator so long as
            // the edit is screwing up future check points.
            while (i < check_points.len) {
                while (token.end < end_position) {
                    if (!buffer->mode.next_token(&buffer->contents, &iterator, &token, &state)) {
                        break;
                    }
                }

                if (token.end > end_position || state != check_points[i].state) {
                    check_points[i].position = token.end;
                    check_points[i].state = state;
                    end_position = check_points[i + 1].position;
                    ++i;
                    if (i == check_points.len) {
                        goto done;
                    }
                } else {
                    break;
                }
            }
        }

        token.end = check_points[i].position;
        state = check_points[i].state;
    }

done:
    cache_window_unified_position(window, window_cache, window->start_position, buffer);
}

void cache_window_unified_create(Editor* editor,
                                 Window_Cache* window_cache,
                                 Window_Unified* window) {
    ZoneScoped;

    WITH_WINDOW_BUFFER(window);
    window_cache->tag = Window::UNIFIED;
    window_cache->v.unified.id = window->id;
    window_cache->v.unified.tokenizer_check_points = {};
    window_cache->v.unified.tokenizer_ran_to_end = false;
    cache_window_unified_update(window_cache, window, buffer);
}

}
}
