#include "overlay_matching_pairs.hpp"

#include <stdlib.h>
#include <algorithm>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include "buffer.hpp"
#include "overlay.hpp"
#include "theme.hpp"
#include "token.hpp"
#include "visible_region.hpp"
#include "window.hpp"

namespace mag {
struct Overlay;

namespace syntax {

struct Data {
    Contents_Iterator iterator;
    size_t index;
    cz::Vector<uint64_t> points;
};

static bool binary_search(cz::Slice<Token> tokens, uint64_t position, size_t* token_index) {
    size_t start = 0;
    size_t end = tokens.len;
    while (start < end) {
        size_t mid = (start + end) / 2;
        if (position < tokens[mid].start) {
            end = mid;
        } else if (position >= tokens[mid].end) {
            start = mid + 1;
        } else {
            *token_index = mid;
            return true;
        }
    }
    return false;
}

static void* overlay_matching_pairs_start_frame(Buffer* buffer, Window_Unified* window) {
    Data* data = (Data*)malloc(sizeof(Data));
    data->iterator = buffer->contents.iterator_at(window->start_position);
    data->index = 0;
    data->points = {};

    Contents_Iterator end_iterator = data->iterator;
    compute_visible_end(window, &end_iterator);

    Tokenizer_Check_Point check_point = {};
    buffer->token_cache.update(buffer);
    buffer->token_cache.find_check_point(data->iterator.position, &check_point);

    cz::Vector<Token> tokens = {};
    tokens.reserve(cz::heap_allocator(), 8);
    CZ_DEFER(tokens.drop(cz::heap_allocator()));
    {
        Token token;
        token.end = check_point.position;
        Contents_Iterator token_iterator = data->iterator;
        token_iterator.retreat(token_iterator.position - token.end);
        uint64_t state = check_point.state;
        while (end_iterator.position >= token.end) {
            bool has_token = buffer->mode.next_token(&token_iterator, &token, &state);
            if (has_token) {
                if (token.type == Token_Type::OPEN_PAIR || token.type == Token_Type::CLOSE_PAIR) {
                    tokens.reserve(cz::heap_allocator(), 1);
                    tokens.push(token);
                }
            } else {
                break;
            }
        }
    }

    cz::Slice<Cursor> cursors = window->cursors;
    data->points.reserve(cz::heap_allocator(), cursors.len * sizeof(uint64_t));
    for (size_t c = 0; c < cursors.len; ++c) {
        size_t token_index;
        if (binary_search(tokens, cursors[c].point, &token_index)) {
            size_t depth = 1;
            if (tokens[token_index].type == Token_Type::OPEN_PAIR) {
                for (size_t i = token_index + 1; i < tokens.len(); ++i) {
                    if (tokens[i].type == Token_Type::OPEN_PAIR) {
                        ++depth;
                    } else {
                        --depth;
                        if (depth == 0) {
                            data->points.push(tokens[i].start);
                            break;
                        }
                    }
                }
            } else {
                for (size_t i = token_index; i-- > 0;) {
                    if (tokens[i].type == Token_Type::CLOSE_PAIR) {
                        ++depth;
                    } else {
                        --depth;
                        if (depth == 0) {
                            data->points.push(tokens[i].start);
                            break;
                        }
                    }
                }
            }
        }
    }

    std::sort(data->points.start(), data->points.end());
    auto new_end = std::unique(data->points.start(), data->points.end());
    data->points.set_len(new_end - data->points.start());

    return data;
}

static Face overlay_matching_pairs_get_face_and_advance(Buffer* buffer,
                                                        Window_Unified* window,
                                                        void* _data) {
    Data* data = (Data*)_data;

    Face face = {};
    if (data->index < data->points.len()) {
        if (data->points[data->index] == data->iterator.position) {
            face = {-1, 237, 0};
            ++data->index;
        }
        data->iterator.advance();
    }
    return face;
}

static void overlay_matching_pairs_cleanup_frame(void* _data) {
    Data* data = (Data*)_data;
    data->points.drop(cz::heap_allocator());
    free(data);
}

Overlay overlay_matching_pairs() {
    return Overlay{
        overlay_matching_pairs_start_frame,
        overlay_matching_pairs_get_face_and_advance,
        overlay_matching_pairs_cleanup_frame,
    };
}

}
}
