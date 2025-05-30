#include "decoration_cursor_count.hpp"

#include <stdlib.h>
#include <algorithm>
#include <cz/defer.hpp>
#include <cz/format.hpp>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/decoration.hpp"
#include "core/editor.hpp"
#include "core/movement.hpp"
#include "core/visible_region.hpp"
#include "core/window.hpp"

namespace mag {
namespace syntax {

static bool decoration_cursor_count_append(Editor* editor,
                                           Client* client,
                                           const Buffer* buffer,
                                           Window_Unified* window,
                                           cz::Allocator allocator,
                                           cz::String* string,
                                           void* _data) {
    ZoneScoped;

    if (window->cursors.len <= 1) {
        return false;
    }

    Contents_Iterator visible_start = buffer->contents.iterator_at(window->start_position);
    Contents_Iterator visible_end = visible_start;
    forward_visual_line(window, buffer->mode, editor->theme, &visible_end, window->rows());

    size_t visible = 0;
    for (size_t i = 0; i < window->cursors.len; ++i) {
        if (window->cursors[i].point >= visible_start.position &&
            window->cursors[i].point <= visible_end.position) {
            ++visible;
        }
    }

    cz::append(allocator, string, '(', window->selected_cursor + 1, '/', window->cursors.len);
    if (visible != window->cursors.len) {
        cz::append(allocator, string, " V:", visible);
    }
    cz::append(allocator, string, ')');
    return true;
}

static void decoration_cursor_count_cleanup(void* _data) {}

Decoration decoration_cursor_count() {
    static const Decoration::VTable vtable = {decoration_cursor_count_append,
                                              decoration_cursor_count_cleanup};
    return {&vtable, nullptr};
}

}
}
