#pragma once

#include <stdint.h>
#include <cz/slice.hpp>
#include <cz/vector.hpp>

namespace mag {

struct SSOStr;

struct Contents_Iterator;

struct Contents {
    cz::Vector<cz::Slice<char>> buckets;
    uint64_t len;

    void drop();

    void remove(uint64_t start, uint64_t len);
    void insert(uint64_t position, cz::Str str);

    cz::String stringify(cz::Allocator allocator) const;
    SSOStr slice(cz::Allocator allocator, uint64_t start, uint64_t end) const;

    char get_once(uint64_t position) const;

    bool is_bucket_separator(uint64_t pos) const;

    Contents_Iterator iterator_at(uint64_t position) const;
};

struct Contents_Iterator {
    const Contents* contents;
    uint64_t position;
    size_t bucket;
    size_t index;

    bool at_bob() const { return position == 0; }
    bool at_eob() const { return position == contents->len; }

    char get() const { return contents->buckets[bucket][index]; }

    void retreat(uint64_t offset = 1) {
        CZ_DEBUG_ASSERT(position >= offset);
        position -= offset;
        if (offset > index) {
            offset -= index;
            CZ_DEBUG_ASSERT(bucket > 0);
            --bucket;
            if (offset <= contents->buckets[bucket].len) {
                index = contents->buckets[bucket].len - offset;
                return;
            } else {
                offset -= contents->buckets[bucket].len;
                CZ_DEBUG_ASSERT(bucket > 0);
                while (offset > contents->buckets[bucket].len) {
                    offset -= contents->buckets[bucket].len;
                    CZ_DEBUG_ASSERT(bucket > 0);
                    --bucket;
                }
                index = contents->buckets[bucket].len - offset;
            }
        } else {
            index -= offset;
        }
    }

    void advance(uint64_t offset = 1) {
        CZ_DEBUG_ASSERT(position + offset <= contents->len);
        position += offset;
        index += offset;
        while (index >= contents->buckets[bucket].len) {
            if (bucket == contents->buckets.len()) {
                CZ_DEBUG_ASSERT(index == contents->buckets[bucket].len);
                break;
            }

            index -= contents->buckets[bucket].len;
            ++bucket;
            if (bucket == contents->buckets.len()) {
                CZ_DEBUG_ASSERT(index == 0);
                break;
            }
        }
    }
};

}
