#include "mini_buffer_results.hpp"

#include <Tracy.hpp>
#include <algorithm>
#include <cz/fs/directory.hpp>
#include <cz/heap.hpp>
#include <cz/util.hpp>

namespace mag {
namespace client {

static bool binary_search_string_prefix_start(cz::Slice<cz::Str> results,
                                              cz::Str prefix,
                                              size_t* out) {
    size_t start = 0;
    size_t end = results.len;
    while (start < end) {
        size_t mid = (start + end) / 2;
        int cmp = memcmp(results[mid].buffer, prefix.buffer, cz::min(results[mid].len, prefix.len));
        if (cmp < 0) {
            start = mid + 1;
        } else if (cmp == 0) {
            if (results[mid].len < prefix.len) {
                // Too short means we need to look at longer strings that are sorted after shorter
                // strings.
                start = mid + 1;
            } else if (end > start + 1) {
                // Even though we found one match, we need to look earlier for a lesser match.
                if (mid + 1 >= end) {
                    CZ_DEBUG_ASSERT(mid > 0);
                    if (results[mid - 1].len > prefix.len &&
                        memcmp(results[mid - 1].buffer, prefix.buffer, prefix.len) == 0) {
                        *out = mid - 1;
                    } else {
                        *out = mid;
                    }
                    return true;
                }
                end = mid + 1;
            } else {
                *out = mid;
                return true;
            }
        } else {
            end = mid;
        }
    }
    return false;
}

static size_t binary_search_string_prefix_end(cz::Slice<cz::Str> results,
                                              size_t start,
                                              cz::Str prefix) {
    size_t end = results.len;
    while (start < end) {
        size_t mid = (start + end) / 2;
        int cmp = memcmp(results[mid].buffer, prefix.buffer, cz::min(results[mid].len, prefix.len));
        if (cmp < 0) {
            CZ_PANIC("Unreachable: sorted list is out of order");
        } else if (cmp == 0) {
            if (end > start + 1) {
                start = mid;
            } else {
                break;
            }
        } else {
            end = mid;
        }
    }
    return end;
}

void load_mini_buffer_results(Mini_Buffer_Results* mini_buffer_results) {
    ZoneScoped;

    switch (mini_buffer_results->response_tag) {
    case Message::RESPOND_FILE: {
        mini_buffer_results->query.reserve(cz::heap_allocator(), 1);
        mini_buffer_results->query.null_terminate();
        char* dir_sep = mini_buffer_results->query.rfind('/');
        cz::Str prefix;
        if (dir_sep) {
            *dir_sep = '\0';
            cz::fs::files(cz::heap_allocator(), cz::heap_allocator(),
                          mini_buffer_results->query.buffer(), &mini_buffer_results->results);
            *dir_sep = '/';
            prefix = dir_sep + 1;
        } else {
            cz::fs::files(cz::heap_allocator(), cz::heap_allocator(), ".",
                          &mini_buffer_results->results);
            prefix = mini_buffer_results->query;
        }
        std::sort(mini_buffer_results->results.start(), mini_buffer_results->results.end());

        size_t start;
        if (binary_search_string_prefix_start(mini_buffer_results->results, prefix, &start)) {
            size_t end =
                binary_search_string_prefix_end(mini_buffer_results->results, start, prefix);
            mini_buffer_results->results.set_len(end);
            mini_buffer_results->results.remove_range(0, start);
        } else {
            mini_buffer_results->results.set_len(0);
        }

        mini_buffer_results->state = Mini_Buffer_Results::LOADED;
        break;
    }

    case Message::RESPOND_BUFFER:
        mini_buffer_results->state = Mini_Buffer_Results::LOADED;
        break;

    case Message::RESPOND_TEXT:
        mini_buffer_results->state = Mini_Buffer_Results::LOADED;
        break;

    case Message::NONE:
    case Message::SHOW:
        CZ_PANIC("");
    }
}

}
}
