#include "overlay_matching_tokens.hpp"

#include <stdint.h>
#include <stdlib.h>
#include <Tracy.hpp>
#include <cz/heap.hpp>
#include "buffer.hpp"
#include "movement.hpp"
#include "overlay.hpp"
#include "theme.hpp"
#include "token.hpp"
#include "window.hpp"

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

    Contents_Iterator cursor_token_iterator;
    uint64_t cursor_token_length;
    Token_Type cursor_token_type;

    Contents_Iterator iterator;
    Token token;
    uint64_t state;
};
}
using namespace overlay_matching_tokens_impl;

static void set_token_matches(Data* data) {
    data->token_matches = false;

    if ((data->token.type == Token_Type::DEFAULT) !=
            (data->cursor_token_type == Token_Type::DEFAULT) ||
        data->token.end - data->token.start != data->cursor_token_length) {
        return;
    }

    Contents_Iterator it1 = data->cursor_token_iterator;
    Contents_Iterator it2 = data->iterator;
    it2.retreat_to(data->token.start);
    for (size_t i = 0; i < data->cursor_token_length; ++i) {
        if (it1.get() != it2.get()) {
            return;
        }
        it1.advance();
        it2.advance();
    }

    data->token_matches = true;
}

static void overlay_matching_tokens_start_frame(const Buffer* buffer,
                                                Window_Unified* window,
                                                Contents_Iterator iterator,
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

    Contents_Iterator visible_start_iterator = iterator;
    Contents_Iterator visible_end_iterator = iterator;
    forward_visual_line(window, buffer->mode, &visible_end_iterator, window->rows() - 1);
    backward_visual_line(window, buffer->mode, &visible_start_iterator, window->rows() - 1);
    if (window->start_position < visible_start_iterator.position ||
        visible_end_iterator.position < window->start_position) {
        return;
    }

    uint64_t cursor_point = window->cursors[window->selected_cursor].point;

    // The token cache is updated in the main render loop.
    CZ_DEBUG_ASSERT(buffer->token_cache.change_index == buffer->changes.len());
    Tokenizer_Check_Point check_point = {};
    buffer->token_cache.find_check_point(window->start_position, &check_point);

    // Use `go_to` because if we're animating then
    // `window->start_position` is much later than the start position.
    iterator.go_to(check_point.position);
    uint64_t state = check_point.state;
    Token token;

    bool found_token = false;

    while (1) {
        if (!buffer->mode.next_token(&iterator, &token, &state)) {
            break;
        }

        // Find the first token on the screen.
        if (!found_token && token.end >= window->start_position) {
            found_token = true;
            data->iterator = iterator;
            data->token = token;
            data->state = state;
        }

        // If the cursor is before the first token then there is no token to match against.
        if (cursor_point < token.start) {
            break;
        }

        // Find the token the cursor is at.
        if (token.start <= cursor_point && cursor_point <= token.end) {
            if (!is_matching_token(data->token_types, token.type)) {
                continue;
            }

            // If the cursor is right after the token, see if another valid matching token is right
            // after the current token.
            if (cursor_point == token.end) {
                Contents_Iterator iterator2 = iterator;
                uint64_t state2 = state;
                Token token2;
                if (buffer->mode.next_token(&iterator2, &token2, &state2)) {
                    if (cursor_point == token2.start &&
                        is_matching_token(data->token_types, token2.type)) {
                        iterator = iterator2;
                        state = state2;
                        token = token2;
                    }
                }
            }

            data->cursor_token_iterator = iterator;
            data->cursor_token_iterator.retreat_to(token.start);
            data->cursor_token_length = token.end - token.start;
            data->cursor_token_type = token.type;

            set_token_matches(data);

            data->enabled = true;
            break;
        }
    }
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
    while (iterator.position >= data->token.end) {
        if (!buffer->mode.next_token(&data->iterator, &data->token, &data->state)) {
            data->enabled = false;
            return {};
        }
        set_token_matches(data);
    }

    // Check if the overlay token matches the cursor token.
    if (data->token_matches && data->token.start <= iterator.position &&
        iterator.position < data->token.end) {
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

static void overlay_matching_tokens_end_frame(void* _data) {}

static void overlay_matching_tokens_cleanup(void* data) {
    cz::heap_allocator().dealloc((Data*)data);
}

Overlay overlay_matching_tokens(Face face, cz::Slice<const Token_Type> types) {
    static const Overlay::VTable vtable = {
        overlay_matching_tokens_start_frame,
        overlay_matching_tokens_get_face_and_advance,
        overlay_matching_tokens_get_face_newline_padding,
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
