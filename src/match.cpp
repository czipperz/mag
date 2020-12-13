#include "match.hpp"

#include <Tracy.hpp>
#include "contents.hpp"

namespace mag {

bool looking_at_no_bounds_check(Contents_Iterator it, cz::Str query) {
    ZoneScoped;

    for (size_t i = 0; i < query.len; ++i) {
        if (it.get() != query[i]) {
            return false;
        }

        it.advance();
    }

    return true;
}

bool looking_at(Contents_Iterator it, cz::Str query) {
    ZoneScoped;

    if (it.position + query.len > it.contents->len) {
        return false;
    }

    return looking_at_no_bounds_check(it, query);
}

bool matches(Contents_Iterator it, uint64_t end, cz::Str query) {
    ZoneScoped;

    if (it.position + query.len != end) {
        return false;
    }

    return looking_at(it, query);
}

bool matches(Contents_Iterator it, uint64_t end, Contents_Iterator query) {
    ZoneScoped;

    CZ_DEBUG_ASSERT(end >= it.position);
    if (query.position + (end - it.position) > query.contents->len) {
        return false;
    }

    while (it.position < end) {
        if (it.get() != query.get()) {
            return false;
        }

        it.advance();
        query.advance();
    }

    return true;
}

bool matches(Contents_Iterator it, uint64_t end, Contents_Iterator query, uint64_t query_end) {
    if (end - it.position != query_end - query.position) {
        return false;
    }
    return matches(it, end, query);
}

}
