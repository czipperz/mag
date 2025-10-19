#include "overlay_build_severities.hpp"

#include <cz/heap.hpp>
#include "core/eat.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/overlay.hpp"

namespace mag {
namespace syntax {

namespace {
struct Data {
    Face line_face;
};
}

// @OverlayBuildSeveritiesParsing
#define MIDDLE_OF_LINE_IDENT_CASES \
    CZ_ALNUM_CASES:                \
    case '_'
#define START_OF_LINE_IDENT_CASES '.' : case '/' : case '-'

static bool is_ident(char ch) {
    switch (ch) {
    case MIDDLE_OF_LINE_IDENT_CASES:
    case START_OF_LINE_IDENT_CASES:
        return true;
    default:
        return false;
    }
}

static Face recalculate_face(Contents_Iterator it) {
    if (it.at_eob() || !is_ident(it.get()))
        return {};
    it.advance();
    for (; !it.at_eob() && is_ident(it.get()); it.advance()) {
    }
    if (!eat_character(&it, ':'))
        return {};
    if (!eat_number(&it))
        return {};
    if (!eat_character(&it, ':'))
        return {};
    if (!eat_number(&it))
        return {};

    if (looking_at(it, ": error:")) {
        return {1, 0, {}};
    } else if (looking_at(it, ": warning:")) {
        return {7, 0, {}};
    } else if (looking_at(it, ": note:")) {
        return {100, 0, {}};
    } else {
        return {};
    }
}

static void start_frame(Editor*,
                        Client*,
                        const Buffer*,
                        Window_Unified*,
                        Contents_Iterator start_position_iterator,
                        void* _data) {
    Data* data = (Data*)_data;
    start_of_line(&start_position_iterator);
    data->line_face = recalculate_face(start_position_iterator);
}
static Face get_face_and_advance(const Buffer*,
                                 Window_Unified*,
                                 Contents_Iterator current_position_iterator,
                                 void* _data) {
    Data* data = (Data*)_data;
    if (at_start_of_line(current_position_iterator)) {
        data->line_face = recalculate_face(current_position_iterator);
    }
    return data->line_face;
}
static Face get_face_newline_padding(const Buffer*,
                                     Window_Unified*,
                                     Contents_Iterator end_of_line_iterator,
                                     void*) {
    return {};
}
static void skip_forward_same_line(const Buffer*,
                                   Window_Unified*,
                                   Contents_Iterator start,
                                   uint64_t end,
                                   void*) {}
static void end_frame(void*) {}
static void cleanup(void* _data) {
    Data* data = (Data*)_data;
    cz::heap_allocator().dealloc(data);
}

Overlay overlay_build_severities() {
    static const Overlay::VTable vtable = {start_frame,
                                           get_face_and_advance,
                                           get_face_newline_padding,
                                           skip_forward_same_line,
                                           end_frame,
                                           cleanup};

    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    *data = {};
    return {&vtable, data};
}

}
}
