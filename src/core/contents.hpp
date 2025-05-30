#pragma once

#include <stdint.h>
#include <cz/allocator.hpp>
#include <cz/assert.hpp>
#include <cz/slice.hpp>
#include <cz/str.hpp>
#include <cz/string.hpp>
#include <cz/vector.hpp>
#include <tracy/Tracy.hpp>

namespace mag {

struct SSOStr;

struct Contents_Iterator;

struct Contents {
    cz::Vector<cz::Slice<char>> buckets;
    cz::Vector<uint64_t> bucket_lfs;
    uint64_t len;

    void drop();

    void remove(uint64_t start, uint64_t len);
    void insert(uint64_t position, cz::Str str);
    void append(cz::Str str);

    cz::String stringify(cz::Allocator allocator) const;
    void stringify_into(cz::Allocator allocator, cz::String* string) const;
    SSOStr slice(cz::Allocator allocator, Contents_Iterator start, uint64_t end) const;
    void slice_into(Contents_Iterator start, uint64_t end, char* string) const;
    void slice_into(Contents_Iterator start, uint64_t end, cz::String* string) const;
    void slice_into(cz::Allocator allocator,
                    Contents_Iterator start,
                    uint64_t end,
                    cz::String* string) const;

    char get_once(uint64_t position) const;
    Contents_Iterator iterator_at(uint64_t position) const;

    /// Note: lines (and columns) are 1-indexed!
    uint64_t get_line_number(uint64_t position) const;

    inline Contents_Iterator start() const;
    inline Contents_Iterator end() const;
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

    void retreat_to(uint64_t new_position) { return retreat(position - new_position); }
    void advance_to(uint64_t new_position) { return advance(new_position - position); }

    void go_to(uint64_t new_position) {
        if (new_position < position) {
            retreat_to(new_position);
        } else {
            advance_to(new_position);
        }
    }

    void retreat() {
        CZ_DEBUG_ASSERT(!at_bob());
        --position;
        if (index == 0) {
            --bucket;
            index = contents->buckets[bucket].len;
        }
        --index;
    }

    void advance() {
        CZ_DEBUG_ASSERT(!at_eob());
        ++position;
        ++index;
        if (index == contents->buckets[bucket].len) {
            ++bucket;
            index = 0;
        }
    }

    void retreat_most(uint64_t offset) {
        if (offset >= position) {
            retreat(offset);
        } else {
            *this = contents->start();
        }
    }

    void advance_most(uint64_t offset) {
        if (position + offset <= contents->len) {
            advance(offset);
        } else {
            *this = contents->end();
        }
    }

    uint64_t get_line_number() const { return contents->get_line_number(position); }
};

inline Contents_Iterator Contents::start() const {
    Contents_Iterator it = {};
    it.contents = this;
    return it;
}

inline Contents_Iterator Contents::end() const {
    Contents_Iterator it = {};
    it.contents = this;
    it.position = len;
    it.bucket = buckets.len;
    it.index = 0;
    return it;
}

}
