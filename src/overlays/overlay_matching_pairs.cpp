#include "overlay_matching_pairs.hpp"

#include <stdlib.h>
#include <cz/binary_search.hpp>
#include <cz/dedup.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/sort.hpp>
#include <cz/vector.hpp>
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/editor.hpp"
#include "core/movement.hpp"
#include "core/overlay.hpp"
#include "core/theme.hpp"
#include "core/token.hpp"
#include "core/token_iterator.hpp"
#include "core/window.hpp"

namespace mag {
namespace syntax {

namespace overlay_matching_pairs_impl {
struct Data {
    Face face;
    size_t index;
    cz::Vector<uint64_t> points;

    cz::Vector<Token> tokens;
    uint64_t cache_start_position;
    uint64_t cache_end_position;
    size_t cache_change_index;
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

static void overlay_matching_pairs_start_frame(Editor* editor,
                                               Client*,
                                               const Buffer* buffer,
                                               Window_Unified* window,
                                               Contents_Iterator start_position_iterator,
                                               void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;
    data->index = 0;
    data->points.len = 0;

    Contents_Iterator end_iterator = start_position_iterator;
    forward_visual_line(window, buffer->mode, editor->theme, &end_iterator, window->rows() - 1);

    if (data->cache_start_position != start_position_iterator.position ||
        data->cache_end_position != end_iterator.position ||
        data->cache_change_index != buffer->token_cache.change_index) {
        data->tokens.len = 0;

        Forward_Token_Iterator it;
        it.init_at_or_after(buffer, start_position_iterator.position);
        for (; it.has_token() && end_iterator.position >= it.token().end; it.next()) {
            if (it.token().type == Token_Type::OPEN_PAIR ||
                it.token().type == Token_Type::CLOSE_PAIR ||
                it.token().type == Token_Type::PREPROCESSOR_IF ||
                it.token().type == Token_Type::PREPROCESSOR_ENDIF) {
                data->tokens.reserve(cz::heap_allocator(), 1);
                data->tokens.push(it.token());
            }
        }
    }

    data->cache_start_position = start_position_iterator.position;
    data->cache_end_position = end_iterator.position;
    data->cache_change_index = buffer->token_cache.change_index;

    cz::Slice<Cursor> cursors = window->cursors;
    data->points.reserve(cz::heap_allocator(), cursors.len * sizeof(uint64_t));
    for (size_t c = 0; c < cursors.len; ++c) {
        size_t token_index;
        if (binary_search(data->tokens, cursors[c].point, &token_index)) {
            data->points.reserve(cz::heap_allocator(),
                                 data->tokens[token_index].end - data->tokens[token_index].start);
            for (uint64_t pos = data->tokens[token_index].start;
                 pos < data->tokens[token_index].end; ++pos) {
                data->points.push(pos);
            }

            size_t depth = 1;
            if (data->tokens[token_index].type == Token_Type::OPEN_PAIR ||
                data->tokens[token_index].type == Token_Type::PREPROCESSOR_IF) {
                for (size_t i = token_index + 1; i < data->tokens.len; ++i) {
                    if (data->tokens[i].type == Token_Type::OPEN_PAIR ||
                        data->tokens[i].type == Token_Type::PREPROCESSOR_IF) {
                        ++depth;
                    } else {
                        --depth;
                        if (depth == 0) {
                            data->points.reserve(cz::heap_allocator(),
                                                 data->tokens[i].end - data->tokens[i].start);
                            for (uint64_t pos = data->tokens[i].start; pos < data->tokens[i].end;
                                 ++pos) {
                                data->points.push(pos);
                            }
                            break;
                        }
                    }
                }
            } else {
                for (size_t i = token_index; i-- > 0;) {
                    if (data->tokens[i].type == Token_Type::CLOSE_PAIR ||
                        data->tokens[i].type == Token_Type::PREPROCESSOR_ENDIF) {
                        ++depth;
                    } else {
                        --depth;
                        if (depth == 0) {
                            data->points.reserve(cz::heap_allocator(),
                                                 data->tokens[i].end - data->tokens[i].start);
                            for (uint64_t pos = data->tokens[i].start; pos < data->tokens[i].end;
                                 ++pos) {
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
    cz::dedup(&data->points);

    // Skip to the first on screen point.
    size_t i = 0;
    for (; i < data->points.len; ++i) {
        if (data->points[i] >= start_position_iterator.position)
            break;
    }
    data->index = i;
}

static Face overlay_matching_pairs_get_face_and_advance(const Buffer* buffer,
                                                        Window_Unified* window,
                                                        Contents_Iterator iterator,
                                                        void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;

    Face face = {};
    if (data->index < data->points.len) {
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

static void overlay_matching_pairs_skip_forward_same_line(const Buffer* buffer,
                                                          Window_Unified* window,
                                                          Contents_Iterator start,
                                                          uint64_t end,
                                                          void* _data) {
    ZoneScoped;
    Data* data = (Data*)_data;
    size_t rel_index;
    cz::binary_search(data->points.slice_start(data->index), end, &rel_index);
    data->index += rel_index;
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
        overlay_matching_pairs_skip_forward_same_line,
        overlay_matching_pairs_end_frame,
        overlay_matching_pairs_cleanup,
    };

    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    *data = {};
    data->face = face;
    data->cache_start_position = -1;
    return {&vtable, data};
}

}
}
