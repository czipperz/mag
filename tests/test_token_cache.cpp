#include "core/token_iterator.hpp"
#include "syntax/tokenize_cplusplus.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("Token_Cache find_check_point") {
    Token_Cache token_cache = {};
    CZ_DEFER(token_cache.drop());

    token_cache.check_points.reserve(cz::heap_allocator(), 4);
    token_cache.check_points.push({1 * 1024, 1});
    token_cache.check_points.push({2 * 1024, 2});
    token_cache.check_points.push({3 * 1024, 3});
    token_cache.check_points.push({4 * 1024, 4});

    for (size_t c = 0; c < token_cache.check_points.len; ++c) {
        uint64_t prev = (c == 0 ? 0 : token_cache.check_points[c - 1].position);
        for (uint64_t i = prev; i < token_cache.check_points[c].position; ++i) {
            INFO(i);
            CHECK(token_cache.find_check_point(i).position == prev);
        }
    }
    uint64_t last_position = token_cache.check_points.last().position;
    for (uint64_t i = last_position; i < last_position + 1024; ++i) {
        INFO(i);
        CHECK(token_cache.find_check_point(i).position == last_position);
    }
}
