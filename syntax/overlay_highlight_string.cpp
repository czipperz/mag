#include "overlay_highlight_string.hpp"

#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include <cz/heap.hpp>
#include "buffer.hpp"
#include "contents.hpp"
#include "face.hpp"
#include "overlay.hpp"
#include "token.hpp"
#include "token_cache.hpp"

namespace mag {
struct Buffer;
struct Window_Unified;

namespace syntax {

namespace overlay_highlight_string_impl {
struct Data {
    bool enabled;
    cz::String string;
    Face face;
    bool case_insensitive;

    Token_Type token_type;
    Contents_Iterator token_it;
    uint64_t token_state;
    Token token_token;

    size_t countdown_cursor_region;
};
}
using namespace overlay_highlight_string_impl;

static void overlay_highlight_string_start_frame(const Buffer* buffer,
                                                 Window_Unified*,
                                                 Contents_Iterator iterator,
                                                 void* _data) {
    Data* data = (Data*)_data;
    data->enabled = true;
    data->countdown_cursor_region = 0;

    if (data->token_type != Token_Type::length) {
        Tokenizer_Check_Point check_point = {};
        buffer->token_cache.find_check_point(iterator.position, &check_point);

        Contents_Iterator it = buffer->contents.iterator_at(check_point.position);
        uint64_t state = check_point.state;
        Token token;
        token.end = it.position;

        while (token.end <= iterator.position) {
            if (!buffer->mode.next_token(&it, &token, &state)) {
                data->enabled = false;
                return;
            }
        }

        data->token_it = it;
        data->token_state = state;
        data->token_token = token;
    }
}

static Face overlay_highlight_string_get_face_and_advance(const Buffer* buffer,
                                                          Window_Unified*,
                                                          Contents_Iterator iterator,
                                                          void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;

    if (!data->enabled) {
        return {};
    }

    if (data->countdown_cursor_region > 0) {
        --data->countdown_cursor_region;
    }

    if (data->countdown_cursor_region == 0) {
        if (data->token_type != Token_Type::length) {
            while (data->token_token.end <= iterator.position) {
                if (!buffer->mode.next_token(&data->token_it, &data->token_token,
                                             &data->token_state)) {
                    data->enabled = false;
                    return {};
                }
            }

            if (data->token_token.type != data->token_type) {
                return {};
            }

            if (iterator.position < data->token_token.start) {
                return {};
            }
        }

        size_t i = 0;
        if (data->case_insensitive) {
            for (i = 0; i < data->string.len() && !iterator.at_eob(); ++i) {
                if (cz::to_lower(data->string[i]) != cz::to_lower(iterator.get())) {
                    break;
                }
                iterator.advance();
            }
        } else {
            for (i = 0; i < data->string.len() && !iterator.at_eob(); ++i) {
                if (data->string[i] != iterator.get()) {
                    break;
                }
                iterator.advance();
            }
        }

        if (i == data->string.len()) {
            data->countdown_cursor_region = data->string.len();
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
    data->string.drop(cz::heap_allocator());
    cz::heap_allocator().dealloc(data);
}

Overlay overlay_highlight_string(Face face,
                                 cz::Str str,
                                 bool case_insensitive,
                                 Token_Type token_type) {
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
    data->string = str.duplicate(cz::heap_allocator());
    data->case_insensitive = case_insensitive;
    data->token_type = token_type;
    return {&vtable, data};
}

}
}
