#pragma once

#include <stdint.h>
#include <cz/vector.hpp>

namespace mag {
struct Buffer;
struct Contents_Iterator;

struct Tokenizer_Check_Point {
    uint64_t position;
    uint64_t state;
};

struct Token_Cache {
    size_t change_index;
    cz::Vector<Tokenizer_Check_Point> check_points;
    bool ran_to_end;

    void drop();

    /// Find the last check point before the start position
    bool find_check_point(uint64_t position, Tokenizer_Check_Point*);

    /// Update the cache based on recent changes
    void update(Buffer* buffer);

    void generate_check_points_until(Buffer* buffer, uint64_t position);

    /// Add a check point onto the end
    bool next_check_point(Buffer* buffer, Contents_Iterator* iterator, uint64_t* state);
};

}
