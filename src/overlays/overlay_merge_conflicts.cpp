#include "overlay_merge_conflicts.hpp"

#include <cz/heap.hpp>
#include "core/match.hpp"
#include "core/movement.hpp"

namespace mag {
namespace syntax {

namespace {
enum State {
    NOTHING,
    AT_LESSERS,
    IN_TOP,
    AT_EQUALS,
    IN_BOTTOM,
    AT_GREATERS,
};

struct Data {
    Face dividers;
    Face top;
    Face bottom;
    State state;
};
}

static void at_newline(Data* data, Contents_Iterator iterator) {
    switch (data->state) {
    case NOTHING: {
        if (looking_at(iterator, "<<<<<<<")) {
            data->state = AT_LESSERS;
        }
    } break;

    case AT_LESSERS:
        data->state = IN_TOP;
        // fallthrough

    case IN_TOP: {
        if (looking_at(iterator, "=======")) {
            data->state = AT_EQUALS;
        } else if (looking_at(iterator, ">>>>>>>")) {
            data->state = AT_GREATERS;
        }
    } break;

    case AT_EQUALS:
        data->state = IN_BOTTOM;
        // fallthrough

    case IN_BOTTOM: {
        if (looking_at(iterator, ">>>>>>>")) {
            data->state = AT_GREATERS;
        }
    } break;

    case AT_GREATERS: {
        data->state = NOTHING;
    } break;
    }
}

static void overlay_merge_conflict_start_frame(Editor*,
                                               Client*,
                                               const Buffer* buffer,
                                               Window_Unified*,
                                               Contents_Iterator start_position_iterator,
                                               void* _data) {
    Data* data = (Data*)_data;
    data->state = NOTHING;
    at_newline(data, start_position_iterator);
}

static Face overlay_merge_conflict_get_face_and_advance(const Buffer* buffer,
                                                        Window_Unified*,
                                                        Contents_Iterator iterator,
                                                        void* _data) {
    Data* data = (Data*)_data;
    switch (data->state) {
    case IN_TOP:
        return data->top;
    case IN_BOTTOM:
        return data->bottom;
    case NOTHING:
        return {};
    default:
        return data->dividers;
    }
}

static Face overlay_merge_conflict_get_face_newline_padding(const Buffer* buffer,
                                                            Window_Unified*,
                                                            Contents_Iterator end_of_line_iterator,
                                                            void* _data) {
    Data* data = (Data*)_data;
    Face face = {};
    switch (data->state) {
    case IN_TOP:
    case IN_BOTTOM:
    case NOTHING:
        break;
    default:
        face = data->dividers;
        break;
    }

    forward_char(&end_of_line_iterator);
    at_newline(data, end_of_line_iterator);
    return face;
}

static void overlay_merge_conflict_skip_forward_same_line(const Buffer*,
                                                          Window_Unified*,
                                                          Contents_Iterator start,
                                                          uint64_t end,
                                                          void* _data) {}

static void overlay_merge_conflict_end_frame(void* _data) {}

static void overlay_merge_conflict_cleanup(void* _data) {
    Data* data = (Data*)_data;
    cz::heap_allocator().dealloc(data);
}

Overlay overlay_merge_conflicts(Face dividers, Face top, Face bottom) {
    static const Overlay::VTable vtable = {
        overlay_merge_conflict_start_frame,
        overlay_merge_conflict_get_face_and_advance,
        overlay_merge_conflict_get_face_newline_padding,
        overlay_merge_conflict_skip_forward_same_line,
        overlay_merge_conflict_end_frame,
        overlay_merge_conflict_cleanup,
    };

    Data* data = cz::heap_allocator().alloc<Data>();
    *data = {};
    data->dividers = dividers;
    data->top = top;
    data->bottom = bottom;

    return {&vtable, data};
}

}
}
