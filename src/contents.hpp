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
    void stringify_into(cz::Allocator allocator, cz::String* string) const;
    SSOStr slice(cz::Allocator allocator, Contents_Iterator start, uint64_t end) const;

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

    void retreat(uint64_t offset = 1);
    void advance(uint64_t offset = 1);
};

inline Contents_Iterator Contents::start() const {
    Contents_Iterator it = {};
    it.contents = this;
    return it;
}

}
