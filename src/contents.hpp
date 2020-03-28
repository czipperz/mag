#pragma once

#include <stdint.h>
#include <Tracy.hpp>
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
    void append(cz::Str str);

    cz::String stringify(cz::Allocator allocator) const;
    void stringify_into(cz::Allocator allocator, cz::String* string) const;
    SSOStr slice(cz::Allocator allocator, Contents_Iterator start, uint64_t end) const;
    void slice_into(Contents_Iterator start, uint64_t end, char* string) const;

    char get_once(uint64_t position) const;
    Contents_Iterator iterator_at(uint64_t position) const;

    inline Contents_Iterator start() const;
};

struct Contents_Iterator {
    const Contents* contents;
    uint64_t position;
    size_t bucket;
    size_t index;

    bool at_bob() const { return position == 0; }
    bool at_eob() const { return position == contents->len; }

    char get() const { return contents->buckets[bucket][index]; }

    void retreat(uint64_t offset);
    void advance(uint64_t offset);

    void retreat() {
        ZoneScopedN("retreat 1");

        CZ_DEBUG_ASSERT(!at_bob());
        --position;
        // :EmptyBuckets Once resolved, convert to if
        while (index == 0) {
            --bucket;
            index = contents->buckets[bucket].len;
        }
        --index;
    }

    void advance() {
        ZoneScopedN("advance 1");

        CZ_DEBUG_ASSERT(!at_eob());
        ++position;
        ++index;
        // :EmptyBuckets Once resolved, convert to if
        while (index == contents->buckets[bucket].len) {
            ++bucket;
            index = 0;
            // :EmptyBuckets Once resolved, delete this
            if (bucket == contents->buckets.len()) {
                break;
            }
        }
    }
};

inline Contents_Iterator Contents::start() const {
    Contents_Iterator it = {};
    it.contents = this;
    return it;
}

}
