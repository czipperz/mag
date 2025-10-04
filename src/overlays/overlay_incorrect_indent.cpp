#include "overlay_incorrect_indent.hpp"

#include <cz/char_type.hpp>
#include <cz/heap.hpp>
#include "core/buffer.hpp"
#include "core/overlay.hpp"
#include "core/window.hpp"

namespace mag {
namespace syntax {

namespace overlay_incorrect_indent_impl {
struct Data {
    Face face;
    bool at_start_of_line;
    uint64_t highlight_countdown;
};
}
using namespace overlay_incorrect_indent_impl;

static void overlay_incorrect_indent_start_frame(Editor*,
                                                 Client*,
                                                 const Buffer* buffer,
                                                 Window_Unified* window,
                                                 Contents_Iterator start_position_iterator,
                                                 void* data) {}

static Face overlay_incorrect_indent_get_face_and_advance(
    const Buffer* buffer,
    Window_Unified* window,
    Contents_Iterator current_position_iterator,
    void* _data) {
    Data* data = (Data*)_data;

    if (data->highlight_countdown > 0) {
        --data->highlight_countdown;
        return data->face;
    }

    char ch = current_position_iterator.get();
    if (ch == '\n') {
        data->at_start_of_line = true;
        return {};
    }

    if (!data->at_start_of_line) {
        return {};
    }

    if (ch != ' ' && ch != '\t') {
        data->at_start_of_line = false;
        return {};
    }

    if (!buffer->mode.use_tabs && ch == '\t') {
        return data->face;
    }

    if (!buffer->mode.use_tabs || ch == '\t') {
        return {};
    }

    Contents_Iterator it = current_position_iterator;
    while (!it.at_eob() && it.get() == ' ') {
        it.advance();
    }

    // Tabs must come before spaces.
    if (!it.at_eob() && it.get() == '\t') {
        CZ_DEBUG_ASSERT(it.position > current_position_iterator.position);
        data->highlight_countdown = it.position - current_position_iterator.position - 1;
        return data->face;
    }

    // We can replace these spaces with a tab.
    if (it.position >= current_position_iterator.position + buffer->mode.tab_width) {
        CZ_DEBUG_ASSERT(buffer->mode.tab_width > 0);
        data->highlight_countdown = buffer->mode.tab_width - 1;
        return data->face;
    }

    return {};
}

static Face overlay_incorrect_indent_get_face_newline_padding(
    const Buffer* buffer,
    Window_Unified* window,
    Contents_Iterator end_of_line_iterator,
    void* data) {
    return {};
}

static void overlay_incorrect_indent_skip_forward_same_line(const Buffer* buffer,
                                                            Window_Unified* window,
                                                            Contents_Iterator start,
                                                            uint64_t end,
                                                            void* _data) {
    ZoneScoped;
    // TODO unless jump is tiny: set not at start of line, set coundown=0
    while (start.position != end) {
        overlay_incorrect_indent_get_face_and_advance(buffer, window, start, _data);
        start.advance();
    }
}

static void overlay_incorrect_indent_end_frame(void* data) {}

static void overlay_incorrect_indent_cleanup(void* data) {
    cz::heap_allocator().dealloc((Data*)data);
}

Overlay overlay_incorrect_indent(Face face) {
    static const Overlay::VTable vtable = {
        overlay_incorrect_indent_start_frame,
        overlay_incorrect_indent_get_face_and_advance,
        overlay_incorrect_indent_get_face_newline_padding,
        overlay_incorrect_indent_skip_forward_same_line,
        overlay_incorrect_indent_end_frame,
        overlay_incorrect_indent_cleanup,
    };

    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    data->face = face;
    data->at_start_of_line = true;
    data->highlight_countdown = 0;
    return {&vtable, data};
}

}
}
