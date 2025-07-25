#include "overlay_matching_tokens.hpp"

#include <stdint.h>
#include <stdlib.h>
#include <cz/char_type.hpp>
#include <cz/heap.hpp>
#include <tracy/Tracy.hpp>
#include "basic/search_commands.hpp"
#include "core/buffer.hpp"
#include "core/client.hpp"
#include "core/command_macros.hpp"
#include "core/editor.hpp"
#include "core/match.hpp"
#include "core/overlay.hpp"
#include "core/theme.hpp"
#include "core/token.hpp"
#include "core/window.hpp"

namespace mag {
namespace syntax {

namespace overlay_matching_region_impl {
struct Data {
    Face face;

    bool enabled;

    Contents_Iterator start_marked_region;

    size_t countdown_cursor_region;

    Case_Handling case_handling;
    bool use_prompt;
    cz::String prompt;
};
}
using namespace overlay_matching_region_impl;

static void overlay_matching_region_start_frame(Editor* editor,
                                                Client* client,
                                                const Buffer* buffer,
                                                Window_Unified* window,
                                                Contents_Iterator start_position_iterator,
                                                void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;

    Cursor cursor = window->cursors[window->selected_cursor];

    // Only enable if we've selected a region.
    data->enabled = window->show_marks && cursor.point != cursor.mark;

    // Disable if the region couldn't match anything else because it is
    // giant.  Comparing the massive string takes a long time.  This happens
    // when the user selects the entire file (ie `command_mark_buffer`).
    if (cursor.end() - cursor.start() > buffer->contents.len / 2) {
        data->enabled = false;
    }

    if (!data->enabled) {
        return;
    }

    data->start_marked_region = start_position_iterator;
    if (data->start_marked_region.at_eob()) {
        data->enabled = false;
    } else {
        if (cursor.start() >= data->start_marked_region.position) {
            data->start_marked_region.advance_to(cursor.start());
        } else {
            data->enabled = false;
        }
    }
    data->countdown_cursor_region = 0;

    data->case_handling = buffer->mode.search_continue_case_handling;
    data->use_prompt = false;
    if (basic::in_interactive_search(client)) {
        data->case_handling = buffer->mode.search_prompt_case_handling;
        data->use_prompt = true;

        Window_Unified* window = client->mini_buffer_window();
        WITH_CONST_WINDOW_BUFFER(window, client);
        data->prompt.len = 0;
        buffer->contents.stringify_into(cz::heap_allocator(), &data->prompt);
    }
}

static Face overlay_matching_region_get_face_and_advance(const Buffer* buffer,
                                                         Window_Unified* window,
                                                         Contents_Iterator iterator,
                                                         void* _data) {
    Data* data = (Data*)_data;

    if (!data->enabled) {
        return {};
    }

    ZoneScoped;

    if (data->countdown_cursor_region > 0) {
        --data->countdown_cursor_region;
    }

    if (data->countdown_cursor_region == 0) {
        if (data->use_prompt) {
            if (looking_at_cased(iterator, data->prompt, data->case_handling)) {
                data->countdown_cursor_region = data->prompt.len;
            }
        } else {
            uint64_t end_marked_region = window->cursors[window->selected_cursor].end();
            if (matches_cased(data->start_marked_region, end_marked_region, iterator,
                              data->case_handling)) {
                data->countdown_cursor_region =
                    end_marked_region - data->start_marked_region.position;
            }
        }
    }

    if (data->countdown_cursor_region > 0) {
        return data->face;
    } else {
        return {};
    }
}

static Face overlay_matching_region_get_face_newline_padding(const Buffer* buffer,
                                                             Window_Unified* window,
                                                             Contents_Iterator iterator,
                                                             void* _data) {
    return {};
}

static void overlay_matching_region_end_frame(void* _data) {}

static void overlay_matching_region_cleanup(void* data) {
    cz::heap_allocator().dealloc((Data*)data);
}

Overlay overlay_matching_region(Face face) {
    static const Overlay::VTable vtable = {
        overlay_matching_region_start_frame,
        overlay_matching_region_get_face_and_advance,
        overlay_matching_region_get_face_newline_padding,
        overlay_matching_region_end_frame,
        overlay_matching_region_cleanup,
    };

    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    *data = {};
    data->face = face;
    return {&vtable, data};
}

}
}
