#include "overlay_highlight_string.hpp"

#include <cz/char_type.hpp>
#include <cz/heap.hpp>
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/contents.hpp"
#include "core/face.hpp"
#include "core/match.hpp"
#include "core/overlay.hpp"
#include "core/token.hpp"
#include "core/token_cache.hpp"

namespace mag {
struct Buffer;
struct Window_Unified;

namespace syntax {

namespace overlay_highlight_string_impl {
struct Data {
    bool enabled;
    cz::String string;
    Face face;
    Case_Handling case_handling;
    Matching_Algo matching_algo;

    Token_Type token_type;
    Contents_Iterator token_it;
    uint64_t token_state;
    Token token_token;

    size_t countdown_cursor_region;
};
}
using namespace overlay_highlight_string_impl;

static void overlay_highlight_string_start_frame(Editor*,
                                                 Client*,
                                                 const Buffer* buffer,
                                                 Window_Unified*,
                                                 Contents_Iterator iterator,
                                                 void* _data) {
    Data* data = (Data*)_data;
    data->enabled = true;
    data->countdown_cursor_region = 0;

    if (data->token_type != Token_Type::length) {
        Tokenizer_Check_Point check_point = buffer->token_cache.find_check_point(iterator.position);

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

            switch (data->matching_algo) {
            case Matching_Algo::CONTAINS:
                if (iterator.position < data->token_token.start)
                    return {};
                break;
            case Matching_Algo::EXACT_MATCH:
                if (iterator.position != data->token_token.start ||
                    data->token_token.end - data->token_token.start != data->string.len)
                    return {};
                break;
            case Matching_Algo::PREFIX:
                if (iterator.position != data->token_token.start)
                    return {};
                break;
            case Matching_Algo::SUFFIX:
                if (iterator.position + data->string.len != data->token_token.end)
                    return {};
                break;
            }
        }

        if (looking_at_cased(iterator, data->string, data->case_handling)) {
            data->countdown_cursor_region = data->string.len;
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

static const Overlay::VTable vtable = {
    overlay_highlight_string_start_frame,
    overlay_highlight_string_get_face_and_advance,
    overlay_highlight_string_get_face_newline_padding,
    overlay_highlight_string_end_frame,
    overlay_highlight_string_cleanup,
};

Overlay overlay_highlight_string(Face face,
                                 cz::Str str,
                                 Case_Handling case_handling,
                                 Token_Type token_type,
                                 Matching_Algo matching_algo) {
    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    data->face = face;
    data->string = str.clone(cz::heap_allocator());
    data->case_handling = case_handling;
    data->token_type = token_type;
    data->matching_algo = matching_algo;
    return {&vtable, data};
}

bool is_overlay_highlight_string(const Overlay& overlay, cz::Str str) {
    if (overlay.vtable != &vtable) {
        return false;
    }

    Data* data = (Data*)overlay.data;
    return data->string == str;
}

}
}
