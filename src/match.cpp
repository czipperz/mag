#include "match.hpp"

#include <Tracy.hpp>
#include "contents.hpp"

namespace mag {

static void resolve_smart_case(cz::Str query, Case_Handling* case_handling) {
    if (*case_handling == Case_Handling::SMART_CASE) {
        for (size_t i = 0; i < query.len; ++i) {
            if (cz::is_upper(query[i])) {
                *case_handling = Case_Handling::CASE_SENSITIVE;
                return;
            }
        }
        *case_handling = Case_Handling::CASE_INSENSITIVE;
    }
}

static void resolve_smart_case(Contents_Iterator query,
                               uint64_t end,
                               Case_Handling* case_handling) {
    if (*case_handling == Case_Handling::SMART_CASE) {
        for (; query.position < end; query.advance()) {
            if (cz::is_upper(query.get())) {
                *case_handling = Case_Handling::CASE_SENSITIVE;
                return;
            }
        }
        *case_handling = Case_Handling::CASE_INSENSITIVE;
    }
}

static bool cased_char_match(char test, char query, Case_Handling case_handling) {
    if (case_handling == Case_Handling::UPPERCASE_STICKY && cz::is_upper(query)) {
        return test == query;
    }

    return cz::to_lower(test) == cz::to_lower(query);
}

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

bool looking_at_cased(Contents_Iterator it, cz::Str query, Case_Handling case_handling) {
    resolve_smart_case(query, &case_handling);
    if (case_handling == Case_Handling::CASE_SENSITIVE) {
        return looking_at(it, query);
    }

    ZoneScoped;

    if (it.position + query.len > it.contents->len) {
        return false;
    }

    for (size_t i = 0; i < query.len; ++i) {
        if (!cased_char_match(it.get(), query[i], case_handling)) {
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
                   Case_Handling case_handling) {
    CZ_DEBUG_ASSERT(end >= it.position);
    if (query.position + (end - it.position) > query.contents->len) {
        return false;
    }

    resolve_smart_case(query, query.position + (end - it.position), &case_handling);
    if (case_handling == Case_Handling::CASE_SENSITIVE) {
        return matches(it, end, query);
    }

    ZoneScoped;

    while (it.position < end) {
        if (!cased_char_match(it.get(), query.get(), case_handling)) {
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

static void resolve_find_case(char ch, Case_Handling* case_handling) {
    switch (*case_handling) {
    case Case_Handling::CASE_SENSITIVE:
    case Case_Handling::CASE_INSENSITIVE:
        break;

    case Case_Handling::UPPERCASE_STICKY:
    case Case_Handling::SMART_CASE:
        if (cz::is_lower(ch)) {
            *case_handling = Case_Handling::CASE_INSENSITIVE;
        } else {
            *case_handling = Case_Handling::CASE_SENSITIVE;
        }
        break;
    }
}

bool find_cased(Contents_Iterator* it, char ch, Case_Handling case_handling) {
    resolve_find_case(ch, &case_handling);
    if (case_handling == Case_Handling::CASE_SENSITIVE || !cz::is_alpha(ch)) {
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

bool rfind_cased(Contents_Iterator* it, char ch, Case_Handling case_handling) {
    resolve_find_case(ch, &case_handling);
    if (case_handling == Case_Handling::CASE_SENSITIVE || !cz::is_alpha(ch)) {
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
        if (!find(it, query[0])) {
            break;
        }

        if (looking_at(*it, query)) {
            return true;
        }

        it->advance();
    }

    return false;
}

bool search_forward_cased(Contents_Iterator* it, cz::Str query, Case_Handling case_handling) {
    resolve_smart_case(query, &case_handling);
    if (case_handling == Case_Handling::CASE_SENSITIVE) {
        return search_forward(it, query);
    }

    ZoneScoped;

    if (query.len == 0) {
        return true;
    }

    while (1) {
        if (!find_cased(it, query[0], case_handling)) {
            break;
        }

        if (looking_at_cased(*it, query, case_handling)) {
            return true;
        }

        it->advance();
    }

    return false;
}

bool search_backward(Contents_Iterator* it, cz::Str query) {
    ZoneScoped;

    if (query.len > it->contents->len) {
        *it = it->contents->start();
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

bool search_backward_cased(Contents_Iterator* it, cz::Str query, Case_Handling case_handling) {
    resolve_smart_case(query, &case_handling);
    if (case_handling == Case_Handling::CASE_SENSITIVE) {
        return search_backward(it, query);
    }

    ZoneScoped;

    if (query.len > it->contents->len) {
        *it = it->contents->start();
        return false;
    }
    if (query.len == 0) {
        return true;
    }

    if (it->contents->len - query.len < it->position) {
        it->retreat_to(it->contents->len - query.len);
    }

    while (1) {
        if (looking_at_cased(*it, query, case_handling)) {
            return true;
        }

        if (!rfind_cased(it, query[0], case_handling)) {
            break;
        }
    }

    return false;
}

}
