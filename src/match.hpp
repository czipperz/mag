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

/// Tests if the buffer at `it` matches `query`.
/// Ignores case differences if `case_insensitive == true`.
bool looking_at_cased(Contents_Iterator it, cz::Str query, bool case_insensitive);

/// Tests if the region from `start` to `end` matches `query`.
bool matches(Contents_Iterator start, uint64_t end, cz::Str query);

/// Tests if the region from `start` to `end` matches `query`.
bool matches(Contents_Iterator start, uint64_t end, Contents_Iterator query);

/// Tests if the region from `start` to `end` matches the region from `query_start` to `query_end`.
bool matches(Contents_Iterator start,
             uint64_t end,
             Contents_Iterator query_start,
             uint64_t query_end);

/// Tests if the region from `start` to `end` matches `query`.
/// Ignores case differences if `case_insensitive == true`.
bool matches_cased(Contents_Iterator start,
                   uint64_t end,
                   Contents_Iterator query,
                   bool case_insensitive);

/// Find a character at or after the point `it`.
/// On success puts `it` at the start of the character.
/// On failure puts `it` at eob and returns `false`.
bool find(Contents_Iterator* it, char ch);
/// Find a character before the point `it`.
/// On success puts `it` at the start of the character.
/// On failure puts `it` at sob and returns `false`.
bool rfind(Contents_Iterator* it, char ch);

/// Same as the functions above except if `case_insensitive == true`, in which case it will look
/// for either `cz::to_lower(ch)` or `cz::to_upper(ch)`, returning the nearest one to `it`.
bool find_cased(Contents_Iterator* it, char ch, bool case_insensitive);
bool rfind_cased(Contents_Iterator* it, char ch, bool case_insensitive);

/// Find `query` at or after the point `it` (will not overlap).
/// On success puts `it` at the start of the match.
/// On failure puts `it` at eob and returns `false`.
bool search_forward(Contents_Iterator* it, cz::Str query);
/// Find `query` starting before the point `it` (the end may be after `it`).
/// On success puts `it` at the start of the match.
/// On failure puts `it` at sob and returns `false`.
bool search_backward(Contents_Iterator* it, cz::Str query);

/// Same as the functions above except if `case_insensitive == true`, in
/// which case it will look for a case insensitive match for `query`.
bool search_forward_cased(Contents_Iterator* it, cz::Str query, bool case_insensitive);
bool search_backward_cased(Contents_Iterator* it, cz::Str query, bool case_insensitive);

}
