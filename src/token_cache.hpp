#pragma once

#include <stdint.h>
#include <cz/arc.hpp>
#include <cz/vector.hpp>

namespace mag {
struct Asynchronous_Job;
struct Buffer;
struct Buffer_Handle;
struct Contents_Iterator;

struct Tokenizer_Check_Point {
    uint64_t position;
    uint64_t state;
};

struct Token_Cache {
    size_t change_index;
    cz::Vector<Tokenizer_Check_Point> check_points;
    bool ran_to_end;

    /// Destroy the token cache.
    void drop();

    /// Duplicate the token cache.
    Token_Cache clone() const;

    /// Reset to the initial state.
    ///
    /// Use this when you are editing the buffer's contents directly.
    /// This isn't needed if you use `Contents::append` though.
    void reset();

    /// Find the last check point before the start position
    bool find_check_point(uint64_t position, Tokenizer_Check_Point*) const;
    bool find_check_point(uint64_t position, size_t* index) const;

    /// Update the cache based on recent changes.  Returns `true` on success, `false`
    /// if a change has invalidated part of the cache and it must be re-generated.
    bool update(const Buffer* buffer);

    /// Check if a position is covered by a check point.
    bool is_covered(uint64_t position) const;

    /// Generate check points until `position` is covered.
    void generate_check_points_until(const Buffer* buffer, uint64_t position);

    /// Add a check point onto the end
    bool next_check_point(const Buffer* buffer, Contents_Iterator* iterator, uint64_t* state);
};

/// Make a job that generates token cache check points
/// for the buffer.  Takes ownership of the `handle`.
Asynchronous_Job job_syntax_highlight_buffer(cz::Arc_Weak<Buffer_Handle> handle);

}
