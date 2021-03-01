#pragma once

#include <stdint.h>
#include <cz/str.hpp>

namespace mag {
struct Contents_Iterator;

/// Tests if the buffer at `it` matches `query`.
///
/// Has undefined behavior if the buffer isn't big enough.
bool looking_at_no_bounds_check(Contents_Iterator it, cz::Str query);

/// Tests if the buffer at `it` matches `query`.
bool looking_at(Contents_Iterator it, cz::Str query);

/// Tests if the region from `start` to `end` matches `query`.
bool matches(Contents_Iterator start, uint64_t end, cz::Str query);

/// Tests if the region from `start` to `end` matches `query`.
bool matches(Contents_Iterator start, uint64_t end, Contents_Iterator query);

/// Tests if the region from `start` to `end` matches the region from `query_start` to `query_end`.
bool matches(Contents_Iterator start,
             uint64_t end,
             Contents_Iterator query_start,
             uint64_t query_end);

}
