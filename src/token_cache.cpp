#include "token_cache.hpp"

#include <stdlib.h>
#include <cz/bit_array.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include "buffer.hpp"
#include "contents.hpp"
#include "token.hpp"

namespace mag {

void Token_Cache::drop() {
    check_points.drop(cz::heap_allocator());
}

bool Token_Cache::find_check_point(uint64_t position, Tokenizer_Check_Point* cp) {
    size_t start = 0;
    size_t end = check_points.len();
    size_t result_index = check_points.len();
    while (start < end) {
        size_t mid = (start + end) / 2;
        if (check_points[mid].position == position) {
            result_index = mid;
            break;
        } else if (check_points[mid].position < position) {
            result_index = mid;
            start = mid + 1;
        } else {
            end = mid;
        }
    }

    if (result_index < check_points.len()) {
        *cp = check_points[result_index];
        return true;
    } else {
        return false;
    }
}

void Token_Cache::update(Buffer* buffer) {
    ZoneScoped;

    cz::Slice<Change> changes = buffer->changes;
    unsigned char* changed_check_points =
        (unsigned char*)calloc(1, cz::bit_array::alloc_size(check_points.len()));
    CZ_DEFER(free(changed_check_points));
    // Detect check points that changed
    for (size_t i = 0; i < check_points.len(); ++i) {
        uint64_t pos = check_points[i].position;

        position_after_changes({changes.elems + change_index, changes.len - change_index}, &pos);

        if (check_points[i].position != pos) {
            cz::bit_array::set(changed_check_points, i);
        }

        check_points[i].position = pos;
    }
    change_index = changes.len;

    Token token;
    token.end = 0;
    uint64_t state = 0;
    // Fix check points that were changed
    for (size_t i = 0; i < check_points.len(); ++i) {
        uint64_t end_position = check_points[i].position;
        if (cz::bit_array::get(changed_check_points, i)) {
            Contents_Iterator iterator = buffer->contents.iterator_at(token.end);
            // Efficiently loop without recalculating the iterator so long as
            // the edit is screwing up future check points.
            while (i < check_points.len()) {
                while (token.end < end_position) {
                    if (!buffer->mode.next_token(&iterator, &token, &state)) {
                        break;
                    }
                }

                if (token.end > end_position || state != check_points[i].state) {
                    check_points[i].position = token.end;
                    check_points[i].state = state;
                    end_position = check_points[i + 1].position;
                    ++i;
                    if (i == check_points.len()) {
                        return;
                    }
                } else {
                    break;
                }
            }
        }

        token.end = check_points[i].position;
        state = check_points[i].state;
    }
}

void Token_Cache::generate_check_points_until(Buffer* buffer, uint64_t position) {
    uint64_t state;
    Contents_Iterator iterator;
    if (check_points.len() > 0) {
        state = check_points.last().state;
        iterator = buffer->contents.iterator_at(check_points.last().position);
    } else {
        state = 0;
        iterator = buffer->contents.start();
    }

    while (iterator.position <= position) {
        if (!next_check_point(buffer, &iterator, &state)) {
            break;
        }
    }
}

bool Token_Cache::next_check_point(Buffer* buffer, Contents_Iterator* iterator, uint64_t* state) {
    ZoneScoped;

    uint64_t start_position = iterator->position;
    while (!iterator->at_eob()) {
        if (iterator->position >= start_position + 1024) {
            Tokenizer_Check_Point check_point;
            check_point.position = iterator->position;
            check_point.state = *state;
            check_points.reserve(cz::heap_allocator(), 1);
            check_points.push(check_point);
            return true;
        }

        Token token;
        if (!buffer->mode.next_token(iterator, &token, state)) {
            break;
        }
    }

    ran_to_end = true;
    return false;
}

}