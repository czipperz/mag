#pragma once

#include <stdint.h>
#include <cz/str.hpp>
#include "case.hpp"

namespace mag {
struct Contents_Iterator;

/// Tests if the buffer at `it` matches `query`.
///
/// Has undefined behavior if the buffer isn't big enough.
bool looking_at_no_bounds_check(Contents_Iterator it, cz::Str query);

/// Tests if the buffer at `it` matches `query`.
bool looking_at(Contents_Iterator it, cz::Str query);

/// Tests if the buffer at `it` matches `query`.
/// Handles case differences based on `case_handling`.
bool looking_at_cased(Contents_Iterator it, cz::Str query, Case_Handling case_handling);

/// Equivalent to the above versions except much faster.
bool looking_at(Contents_Iterator it, char query);
bool looking_at_cased(Contents_Iterator it, char query, Case_Handling case_handling);

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
/// Handles case differences based on `case_handling`.
bool matches_cased(Contents_Iterator start,
                   uint64_t end,
                   Contents_Iterator query,
                   Case_Handling case_handling);
bool matches_cased(Contents_Iterator start,
                   uint64_t end,
                   cz::Str query,
                   Case_Handling case_handling);

/// Find a character at or after the point `it`.
/// On success puts `it` at the start of the character.
/// On failure puts `it` at eob and returns `false`.
bool find(Contents_Iterator* it, char ch);
/// Find a character before the point `it`.
/// On success puts `it` at the start of the character.
/// On failure puts `it` at sob and returns `false`.
bool rfind(Contents_Iterator* it, char ch);

/// Same as the functions above except handles case differences according to `case_handling`.
bool find_cased(Contents_Iterator* it, char ch, Case_Handling case_handling);
bool rfind_cased(Contents_Iterator* it, char ch, Case_Handling case_handling);

/// Find a character at or after the point `it` in the current bucket.
/// On success puts `it` at the start of the character.
/// On failure puts `it` at the start of the next bucket and returns `false`.
bool find_bucket(Contents_Iterator* it, char ch);
/// Find a character before the point `it` in the current bucket.
/// If `it` is at the start of a bucket then searches in the previous bucket.
/// On success puts `it` at the start of the character.
/// On failure puts `it` at the start of the bucket and returns `false`.
bool rfind_bucket(Contents_Iterator* it, char ch);

/// Same as the functions above except handles case differences according to `case_handling`.
bool find_bucket_cased(Contents_Iterator* it, char ch, Case_Handling case_handling);
bool rfind_bucket_cased(Contents_Iterator* it, char ch, Case_Handling case_handling);

/// Find `query` at or after the point `it` (will not overlap).
/// On success puts `it` at the start of the match.
/// On failure puts `it` at eob and returns `false`.
bool find(Contents_Iterator* it, cz::Str query);
/// Find `query` starting before the point `it` (the end may be after `it`).
/// On success puts `it` at the start of the match.
/// On failure puts `it` at sob and returns `false`.
bool rfind(Contents_Iterator* it, cz::Str query);

/// Same as the functions above except handles case differences according to `case_handling`.
bool find_cased(Contents_Iterator* it, cz::Str query, Case_Handling case_handling);
bool rfind_cased(Contents_Iterator* it, cz::Str query, Case_Handling case_handling);

/// Same as the functions above except only searches in the current bucket.
/// See `find_bucket` / `rfind_bucket`.
bool find_bucket(Contents_Iterator* it, cz::Str query);
bool rfind_bucket(Contents_Iterator* it, cz::Str query);

/// Same as the functions above except handles case differences according to `case_handling`.
bool find_bucket_cased(Contents_Iterator* it, cz::Str query, Case_Handling case_handling);
bool rfind_bucket_cased(Contents_Iterator* it, cz::Str query, Case_Handling case_handling);

}
