#include "overlay_matching_pairs.hpp"

#include <stdlib.h>
#include <Tracy.hpp>
#include <algorithm>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/sort.hpp>
#include <cz/vector.hpp>
#include "buffer.hpp"
#include "overlay.hpp"
#include "theme.hpp"
#include "token.hpp"
#include "visible_region.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

namespace overlay_matching_pairs_impl {
struct Data {
    Face face;
    size_t index;
    cz::Vector<uint64_t> points;
};
}
using namespace overlay_matching_pairs_impl;

static bool binary_search(cz::Slice<Token> tokens, uint64_t position, size_t* token_index) {
    size_t start = 0;
    size_t end = tokens.len;
    size_t backup;
    bool has_backup = false;
    while (start < end) {
        size_t mid = (start + end) / 2;
        if (position < tokens[mid].start) {
            end = mid;
        } else if (position > tokens[mid].end) {
            start = mid + 1;
        } else if (position == tokens[mid].end) {
            backup = mid;
            has_backup = true;
            start = mid + 1;
        } else {
            *token_index = mid;
            return true;
        }
    }

    if (has_backup) {
        *token_index = backup;
        return true;
    }
    return false;
}

static void overlay_matching_pairs_start_frame(const Buffer* buffer,
                                               Window_Unified* window,
                                               Contents_Iterator start_position_iterator,
                                               void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;
    data->index = 0;
    data->points.set_len(0);

    Contents_Iterator end_iterator = start_position_iterator;
    compute_visible_end(window, &end_iterator);

    // Note: we update the token cache in the render loop.
    CZ_DEBUG_ASSERT(buffer->token_cache.change_index == buffer->changes.len());
    Tokenizer_Check_Point check_point = {};
    buffer->token_cache.find_check_point(start_position_iterator.position, &check_point);

    cz::Vector<Token> tokens = {};
    tokens.reserve(cz::heap_allocator(), 8);
    CZ_DEFER(tokens.drop(cz::heap_allocator()));
    {
        Token token;
        token.end = check_point.position;
        Contents_Iterator token_iterator = start_position_iterator;
        token_iterator.retreat_to(token.end);
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
                            data->points.reserve(cz::heap_allocator(),
                                                 tokens[i].end - tokens[i].start);
                            for (uint64_t pos = tokens[i].start; pos < tokens[i].end; ++pos) {
                                data->points.push(pos);
                            }
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
                            data->points.reserve(cz::heap_allocator(),
                                                 tokens[i].end - tokens[i].start);
                            for (uint64_t pos = tokens[i].start; pos < tokens[i].end; ++pos) {
                                data->points.push(pos);
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    cz::sort(data->points);
    auto new_end = std::unique(data->points.start(), data->points.end());
    data->points.set_len(new_end - data->points.start());
}

static Face overlay_matching_pairs_get_face_and_advance(const Buffer* buffer,
                                                        Window_Unified* window,
                                                        Contents_Iterator iterator,
                                                        void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;

    Face face = {};
    if (data->index < data->points.len()) {
        if (data->points[data->index] == iterator.position) {
            face = data->face;
            ++data->index;
        }
    }
    return face;
}

static Face overlay_matching_pairs_get_face_newline_padding(const Buffer* buffer,
                                                            Window_Unified* window,
                                                            Contents_Iterator iterator,
                                                            void* _data) {
    return {};
}

static void overlay_matching_pairs_end_frame(void* _data) {}

static void overlay_matching_pairs_cleanup(void* _data) {
    Data* data = (Data*)_data;
    data->points.drop(cz::heap_allocator());
    cz::heap_allocator().dealloc(data);
}

Overlay overlay_matching_pairs(Face face) {
    static const Overlay::VTable vtable = {
        overlay_matching_pairs_start_frame,
        overlay_matching_pairs_get_face_and_advance,
        overlay_matching_pairs_get_face_newline_padding,
        overlay_matching_pairs_end_frame,
        overlay_matching_pairs_cleanup,
    };

    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    *data = {};
    data->face = face;
    return {&vtable, data};
}

}
}
