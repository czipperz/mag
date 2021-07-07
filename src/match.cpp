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

bool looking_at_cased(Contents_Iterator it, cz::Str query, bool case_insensitive) {
    if (!case_insensitive) {
        return looking_at(it, query);
    }

    ZoneScoped;

    if (it.position + query.len > it.contents->len) {
        return false;
    }

    for (size_t i = 0; i < query.len; ++i) {
        if (cz::to_lower(it.get()) != cz::to_lower(query[i])) {
            return false;
        }

        it.advance();
    }

    return true;
}

bool matches(Contents_Iterator it, uint64_t end, cz::Str query) {
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

bool matches_cased(Contents_Iterator it,
                   uint64_t end,
                   Contents_Iterator query,
                   bool case_insensitive) {
    if (!case_insensitive) {
        return matches(it, end, query);
    }

    ZoneScoped;

    CZ_DEBUG_ASSERT(end >= it.position);
    if (query.position + (end - it.position) > query.contents->len) {
        return false;
    }

    while (it.position < end) {
        if (cz::to_lower(it.get()) != cz::to_lower(query.get())) {
            return false;
        }

        it.advance();
        query.advance();
    }

    return true;
}

bool find(Contents_Iterator* it, char ch) {
    ZoneScoped;

    while (1) {
        if (it->at_eob()) {
            return false;
        }

        auto bucket = it->contents->buckets[it->bucket];
        cz::Str str = cz::Str{bucket.elems, bucket.len}.slice_start(it->index);
        const char* ptr = str.find(ch);

        if (ptr) {
            it->advance(ptr - str.buffer);
            return true;
        } else {
            it->advance(str.len);
        }
    }
}

bool rfind(Contents_Iterator* it, char ch) {
    ZoneScoped;

    if (it->bucket == it->contents->buckets.len()) {
        if (it->bucket == 0) {
            return false;
        }

        --it->bucket;
        it->index = it->contents->buckets[it->bucket].len;
    }

    while (1) {
        auto bucket = it->contents->buckets[it->bucket];
        cz::Str str = cz::Str{bucket.elems, bucket.len}.slice_end(it->index);
        const char* ptr = str.rfind(ch);

        if (ptr) {
            it->index = ptr - str.buffer;
            it->position -= str.end() - ptr;
            return true;
        } else {
            if (it->bucket == 0) {
                it->position -= it->index;
                it->index = 0;
                return false;
            }

            it->position -= str.len;
            --it->bucket;
            it->index = it->contents->buckets[it->bucket].len;
        }
    }
}

bool find_cased(Contents_Iterator* it, char ch, bool case_insensitive) {
    if (!case_insensitive || !cz::is_alpha(ch)) {
        return find(it, ch);
    }

    ZoneScoped;

    char lower = cz::to_lower(ch);
    char upper = cz::to_upper(ch);

    while (1) {
        if (it->at_eob()) {
            return false;
        }

        auto bucket = it->contents->buckets[it->bucket];
        cz::Str str = cz::Str{bucket.elems, bucket.len}.slice_start(it->index);
        const char* ptr = str.find(lower);
        const char* ptr2 = str.find(upper);
        if (!ptr) {
            // No lower case result.
            ptr = ptr2;
        } else if (ptr2 && ptr > ptr2) {
            // The upper case result is closer than the lower case result.
            ptr = ptr2;
        }

        if (ptr) {
            it->advance(ptr - str.buffer);
            return true;
        } else {
            it->advance(str.len);
        }
    }
}

bool rfind_cased(Contents_Iterator* it, char ch, bool case_insensitive) {
    if (!case_insensitive || !cz::is_alpha(ch)) {
        return rfind(it, ch);
    }

    ZoneScoped;

    char lower = cz::to_lower(ch);
    char upper = cz::to_upper(ch);

    while (1) {
        auto bucket = it->contents->buckets[it->bucket];
        cz::Str str = cz::Str{bucket.elems, bucket.len}.slice_end(it->index);
        const char* ptr = str.rfind(lower);
        const char* ptr2 = str.rfind(upper);
        if (!ptr) {
            // No lower case result.
            ptr = ptr2;
        } else if (ptr2 && ptr < ptr2) {
            // The upper case result is closer than the lower case result.
            ptr = ptr2;
        }

        if (ptr) {
            it->index = ptr - str.buffer;
            it->position -= str.end() - ptr;
            return true;
        } else {
            if (it->bucket == 0) {
                it->position -= it->index;
                it->index = 0;
                return false;
            }

            --it->bucket;
            it->index = it->contents->buckets[it->bucket].len;
            it->position -= str.len;
        }
    }
}

bool search_forward(Contents_Iterator* it, cz::Str query) {
    ZoneScoped;

    if (query.len == 0) {
        return true;
    }

    while (1) {
        if (looking_at(*it, query)) {
            return true;
        }

        it->advance();
        if (!find(it, query[0])) {
            break;
        }
    }

    return false;
}

bool search_forward_cased(Contents_Iterator* it, cz::Str query, bool case_insensitive) {
    if (!case_insensitive) {
        return search_forward(it, query);
    }

    ZoneScoped;

    if (query.len == 0) {
        return true;
    }

    while (1) {
        if (looking_at_cased(*it, query, case_insensitive)) {
            return true;
        }

        it->advance();
        if (!find_cased(it, query[0], case_insensitive)) {
            break;
        }
    }

    return false;
}

bool search_backward(Contents_Iterator* it, cz::Str query) {
    ZoneScoped;

    if (query.len > it->contents->len) {
        return false;
    }
    if (query.len == 0) {
        return true;
    }

    if (it->contents->len - query.len < it->position) {
        it->retreat_to(it->contents->len - query.len);
    }

    while (1) {
        if (looking_at(*it, query)) {
            return true;
        }

        if (!rfind(it, query[0])) {
            break;
        }
    }

    return false;
}

bool search_backward_cased(Contents_Iterator* it, cz::Str query, bool case_insensitive) {
    if (!case_insensitive) {
        return search_backward(it, query);
    }

    ZoneScoped;

    if (query.len > it->contents->len) {
        return false;
    }
    if (query.len == 0) {
        return true;
    }

    if (it->contents->len - query.len < it->position) {
        it->retreat_to(it->contents->len - query.len);
    }

    while (1) {
        if (looking_at_cased(*it, query, case_insensitive)) {
            return true;
        }

        if (!rfind_cased(it, query[0], case_insensitive)) {
            break;
        }
    }

    return false;
}

}
