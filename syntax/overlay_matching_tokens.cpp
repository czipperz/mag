#include "overlay_matching_tokens.hpp"

#include <stdint.h>
#include <stdlib.h>
#include "buffer.hpp"
#include "overlay.hpp"
#include "theme.hpp"
#include "token.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

static bool is_matching_token(Token_Type type) {
    return type == Token_Type::KEYWORD || type == Token_Type::TYPE ||
           type == Token_Type::PUNCTUATION || type == Token_Type::IDENTIFIER;
}

struct Data {
    Face face;

    bool has_token;
    bool has_cursor_token;

    Token cursor_token;
    uint64_t cursor_state;
    Contents_Iterator cursor_token_iterator;

    Token token;
    uint64_t state;
    Contents_Iterator token_iterator;
    Contents_Iterator iterator;
};

static void* overlay_matching_tokens_start_frame(Buffer* buffer, Window_Unified* window) {
    Data* data = (Data*)malloc(sizeof(Data));
    data->face = {-1, 237, 0};
    data->has_token = false;
    data->has_cursor_token = false;

    buffer->token_cache.update(buffer);
    Tokenizer_Check_Point check_point = {};
    buffer->token_cache.find_check_point(window->start_position, &check_point);
    data->cursor_token_iterator = buffer->contents.iterator_at(check_point.position);
    data->cursor_token.end = data->cursor_token_iterator.position;
    data->iterator = data->cursor_token_iterator;
    if (data->iterator.at_eob()) {
        return data;
    }
    data->iterator.advance(window->start_position - data->iterator.position);

    cz::Slice<Cursor> cursors = window->cursors;
    if (window->show_marks) {
    } else if (cursors.len == 1) {
        data->has_cursor_token = true;
        data->cursor_state = check_point.state;
        while (data->has_cursor_token && cursors[0].point >= data->cursor_token.end) {
            bool cache = false;
            bool cache_has_token;
            Contents_Iterator cache_token_iterator;
            Token cache_token;
            uint64_t cache_state;
            // Don't actually advance if iterator is pointing directly after a matching token
            // and isn't at the start of a matching token.
            if (cursors[0].point == data->cursor_token.end &&
                is_matching_token(data->cursor_token.type)) {
                cache = true;
                cache_has_token = data->has_cursor_token;
                cache_token_iterator = data->cursor_token_iterator;
                cache_token = data->cursor_token;
                cache_state = data->cursor_state;
            }

            data->has_cursor_token = buffer->mode.next_token(
                &data->cursor_token_iterator, &data->cursor_token, &data->cursor_state);

            if (data->iterator.position >= data->cursor_token.start) {
                data->has_token = true;
                data->token = data->cursor_token;
                data->state = data->cursor_state;
                data->token_iterator = data->cursor_token_iterator;
            }

            if (cache && !(data->has_cursor_token && is_matching_token(data->cursor_token.type) &&
                           data->cursor_token.start <= cursors[0].point)) {
                data->has_cursor_token = cache_has_token;
                data->cursor_token_iterator = cache_token_iterator;
                data->cursor_token = cache_token;
                data->cursor_state = cache_state;
                break;
            }
        }

        if (data->has_cursor_token) {
            if (cursors[0].point >= data->cursor_token.start) {
                data->cursor_token_iterator.retreat(data->cursor_token_iterator.position -
                                                    data->cursor_token.start);
            } else {
                data->has_cursor_token = false;
            }
        }
    }

    return data;
}

static Face overlay_matching_tokens_get_face_and_advance(Buffer* buffer,
                                                         Window_Unified* window,
                                                         void* _data) {
    Data* data = (Data*)_data;

    // Recalculate if the current token (`token`) matches the token at the cursor
    // (`cursor_token`)
    bool token_matches_cursor_token = false;
    // Todo: optimize this.  This should use a countdown similar to the loop above to not rerun this
    // over and over.
    if (data->has_token && data->has_cursor_token && data->iterator.position >= data->token.start) {
        uint64_t len = data->token.end - data->token.start;
        if (data->token.type == data->cursor_token.type && is_matching_token(data->token.type) &&
            len == data->cursor_token.end - data->cursor_token.start) {
            Contents_Iterator cti = data->cursor_token_iterator;
            Contents_Iterator it = data->token_iterator;
            it.retreat(it.position - data->token.start);

            size_t i = 0;
            for (; i < len; ++i) {
                if (cti.get() != it.get()) {
                    break;
                }
                cti.advance();
                it.advance();
            }

            if (i == len) {
                token_matches_cursor_token = true;
            }
        }
    }

    data->iterator.advance();
    while (data->has_token && data->iterator.position >= data->token.end) {
        data->has_token =
            buffer->mode.next_token(&data->token_iterator, &data->token, &data->state);
    }

    if (token_matches_cursor_token) {
        return data->face;
    } else {
        return {};
    }
}

static void overlay_matching_tokens_cleanup_frame(void* data) {
    free(data);
}

Overlay overlay_matching_tokens() {
    return Overlay{
        overlay_matching_tokens_start_frame,
        overlay_matching_tokens_get_face_and_advance,
        overlay_matching_tokens_cleanup_frame,
    };
}

}
}