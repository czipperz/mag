#pragma once

#include <stdint.h>
#include <cz/slice.hpp>
#include <cz/vector.hpp>

namespace mag {

struct SSOStr;

struct Contents {
    cz::Vector<cz::Slice<char>> buckets;

    void drop();

    void remove(uint64_t start, uint64_t len);
    void insert(uint64_t position, cz::Str str);

    cz::String stringify(cz::Allocator allocator) const;
    SSOStr slice(cz::Allocator allocator, uint64_t start, uint64_t end) const;

    char operator[](uint64_t position) const;
    uint64_t len() const;

    bool is_bucket_separator(uint64_t pos) const;

    void get_bucket(uint64_t position, size_t* bucket, size_t* index) const;
};

}
