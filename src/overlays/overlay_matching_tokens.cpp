#include "overlay_matching_tokens.hpp"

#include <stdint.h>
#include <stdlib.h>
#include <cz/heap.hpp>
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/editor.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/overlay.hpp"
#include "core/theme.hpp"
#include "core/token.hpp"
#include "core/token_iterator.hpp"
#include "core/window.hpp"

namespace mag {
namespace syntax {

static bool is_matching_token(cz::Slice<const Token_Type> types, Token_Type type) {
    for (size_t i = 0; i < types.len; ++i) {
        if (type == types[i]) {
            return true;
        }
    }
    return false;
}

namespace overlay_matching_tokens_impl {
struct Data {
    Face face;
    cz::Slice<const Token_Type> token_types;

    bool enabled;
    bool token_matches;

    /// At initialization we find the token the cursor is at.
    Contents_Iterator cursor_token_iterator;
    uint64_t cursor_token_length;
    Token_Type cursor_token_type;

    /// Then we compare it to the token at the render point.
    Forward_Token_Iterator iterator;
};
}
using namespace overlay_matching_tokens_impl;

static void set_token_matches(Data* data) {
    data->token_matches = false;

    if ((data->iterator.token().type == Token_Type::DEFAULT) !=
        (data->cursor_token_type == Token_Type::DEFAULT)) {
        return;
    }

    if (!matches(data->cursor_token_iterator,
                 data->cursor_token_iterator.position + data->cursor_token_length,
                 data->iterator.iterator_at_token_start(), data->iterator.token().end)) {
        return;
    }

    data->token_matches = true;
}

static void overlay_matching_tokens_start_frame(Editor* editor,
                                                Client*,
                                                const Buffer* buffer,
                                                Window_Unified* window,
                                                Contents_Iterator start_position_iterator,
                                                void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;
    data->enabled = false;

    if (window->show_marks) {
        return;
    }

    if (buffer->contents.len == 0) {
        return;
    }

    // If we're animating scrolling then `window->start_position` is much later
    // than the start position and we can just disable until that finishes.
    Contents_Iterator visible_start_iterator = start_position_iterator;
    Contents_Iterator visible_end_iterator = start_position_iterator;
    forward_visual_line(window, buffer->mode, editor->theme, &visible_end_iterator,
                        window->rows() - 1);
    backward_visual_line(window, buffer->mode, editor->theme, &visible_start_iterator,
                         window->rows() - 1);
    if (window->start_position < visible_start_iterator.position ||
        visible_end_iterator.position < window->start_position) {
        return;
    }

    uint64_t cursor_point = window->cursors[window->selected_cursor].point;

    // The token cache is updated in the main render loop.
    CZ_DEBUG_ASSERT(buffer->token_cache.change_index == buffer->changes.len);

    if (!data->iterator.init_at_or_after(buffer, window->start_position)) {
        return;
    }

    Forward_Token_Iterator cursor_iterator;
    // We want to be able to use the token immediately before the cursor or
    // the one that the cursor is in.  Thus we do 'cursor_point - 1' here.
    if (!cursor_iterator.init_at_or_after(buffer, cz::max(cursor_point, (uint64_t)1) - 1) ||
        cursor_iterator.token().start > cursor_point) {
        return;
    }
    // If the cursor is right after the token, see if another
    // valid matching token is right after the current token.
    if (cursor_point == cursor_iterator.token().end) {
        Forward_Token_Iterator cursor_iterator2 = cursor_iterator;
        if (cursor_iterator2.next() && cursor_point == cursor_iterator2.token().start &&
            is_matching_token(data->token_types, cursor_iterator2.token().type)) {
            cursor_iterator = cursor_iterator2;
        }
    }

    if (!is_matching_token(data->token_types, cursor_iterator.token().type)) {
        return;
    }

    data->cursor_token_iterator = cursor_iterator.iterator_at_token_start();
    data->cursor_token_length = cursor_iterator.token().end - cursor_iterator.token().start;
    data->cursor_token_type = cursor_iterator.token().type;

    set_token_matches(data);

    data->enabled = true;
}

static Face overlay_matching_tokens_get_face_and_advance(const Buffer* buffer,
                                                         Window_Unified* window,
                                                         Contents_Iterator iterator,
                                                         void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;
    if (!data->enabled) {
        return {};
    }

    // Find the next token if we are overlaying past the current token.
    if (iterator.position >= data->iterator.token().end) {
        if (!data->iterator.next()) {
            data->enabled = false;
            return {};
        }
        set_token_matches(data);
    }

    // Check if the overlay token matches the cursor token.
    if (data->token_matches && data->iterator.token().start <= iterator.position &&
        iterator.position < data->iterator.token().end) {
        return data->face;
    } else {
        return {};
    }
}

static Face overlay_matching_tokens_get_face_newline_padding(const Buffer* buffer,
                                                             Window_Unified* window,
                                                             Contents_Iterator iterator,
                                                             void* _data) {
    return {};
}

static void overlay_matching_tokens_skip_forward_same_line(const Buffer* buffer,
                                                           Window_Unified* window,
                                                           Contents_Iterator start,
                                                           uint64_t end,
                                                           void* _data) {
    ZoneScoped;
    Data* data = (Data*)_data;
    if (!data->enabled)
        return;
    if (!data->iterator.init_at_or_after(buffer, end)) {
        data->enabled = false;
        return;
    }
    set_token_matches(data);
}

static void overlay_matching_tokens_end_frame(void* _data) {}

static void overlay_matching_tokens_cleanup(void* data) {
    cz::heap_allocator().dealloc((Data*)data);
}

Overlay overlay_matching_tokens(Face face, cz::Slice<const Token_Type> types) {
    static const Overlay::VTable vtable = {
        overlay_matching_tokens_start_frame,
        overlay_matching_tokens_get_face_and_advance,
        overlay_matching_tokens_get_face_newline_padding,
        overlay_matching_tokens_skip_forward_same_line,
        overlay_matching_tokens_end_frame,
        overlay_matching_tokens_cleanup,
    };

    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    *data = {};
    data->face = face;
    data->token_types = types;
    return {&vtable, data};
}

}
}
