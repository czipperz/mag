#include "overlay_highlight_string.hpp"

#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include <cz/heap.hpp>
#include "contents.hpp"
#include "face.hpp"
#include "overlay.hpp"

namespace mag {
struct Buffer;
struct Window_Unified;

namespace syntax {

namespace overlay_highlight_string_impl {
struct Data {
    cz::Str str;
    Face face;
    bool case_insensitive;
    size_t countdown_cursor_region;
};
}
using namespace overlay_highlight_string_impl;

static void overlay_highlight_string_start_frame(const Buffer*,
                                                 Window_Unified*,
                                                 Contents_Iterator start_position_iterator,
                                                 void* _data) {
    Data* data = (Data*)_data;
    data->countdown_cursor_region = 0;
}

static Face overlay_highlight_string_get_face_and_advance(const Buffer*,
                                                          Window_Unified*,
                                                          Contents_Iterator iterator,
                                                          void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;

    if (data->countdown_cursor_region > 0) {
        --data->countdown_cursor_region;
    }

    if (data->countdown_cursor_region == 0) {
        size_t i = 0;
        if (data->case_insensitive) {
            for (i = 0; i < data->str.len && !iterator.at_eob(); ++i) {
                if (cz::to_lower(data->str[i]) != cz::to_lower(iterator.get())) {
                    break;
                }
                iterator.advance();
            }
        } else {
            for (i = 0; i < data->str.len && !iterator.at_eob(); ++i) {
                if (data->str[i] != iterator.get()) {
                    break;
                }
                iterator.advance();
            }
        }

        if (i == data->str.len) {
            data->countdown_cursor_region = data->str.len;
        }
    }

    if (data->countdown_cursor_region > 0) {
        return data->face;
    } else {
        return {};
    }
}

static Face overlay_highlight_string_get_face_newline_padding(
    const Buffer*,
    Window_Unified*,
    Contents_Iterator end_of_line_iterator,
    void*) {
    return {};
}

static void overlay_highlight_string_end_frame(void* data) {}

static void overlay_highlight_string_cleanup(void* _data) {
    Data* data = (Data*)_data;
    cz::heap_allocator().dealloc(data);
}

Overlay overlay_highlight_string(Face face, cz::Str str, bool case_insensitive) {
    static const Overlay::VTable vtable = {
        overlay_highlight_string_start_frame,
        overlay_highlight_string_get_face_and_advance,
        overlay_highlight_string_get_face_newline_padding,
        overlay_highlight_string_end_frame,
        overlay_highlight_string_cleanup,
    };

    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    data->face = face;
    data->str = str;
    data->case_insensitive = case_insensitive;
    return {&vtable, data};
}

}
}
