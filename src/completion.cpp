#include "completion.hpp"

#include <Tracy.hpp>
#include <algorithm>
#include <cz/defer.hpp>
#include <cz/fs/directory.hpp>
#include <cz/heap.hpp>
#include <cz/util.hpp>
#include "buffer.hpp"
#include "editor.hpp"

namespace mag {

void Completion_Results::drop() {
    if (cleanup) {
        cleanup(data);
    }
    query.drop(cz::heap_allocator());
    results_buffer_array.drop();
    results.drop(cz::heap_allocator());
}

bool binary_search_string_prefix_start(cz::Slice<cz::Str> results, cz::Str prefix, size_t* out) {
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

size_t binary_search_string_prefix_end(cz::Slice<cz::Str> results, size_t start, cz::Str prefix) {
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

void file_completion_engine(Editor* _editor, Completion_Results* completion_results) {
    ZoneScoped;

    completion_results->results_buffer_array.clear();
    completion_results->results.set_len(0);

    completion_results->query.reserve(cz::heap_allocator(), 1);
    completion_results->query.null_terminate();
    char* dir_sep = completion_results->query.rfind('/');
    cz::Str prefix;
    if (dir_sep) {
        if (dir_sep != completion_results->query.buffer()) {
            // Normal case: "./u" or "/a/b/c".
            *dir_sep = '\0';
            cz::fs::files(cz::heap_allocator(),
                          completion_results->results_buffer_array.allocator(),
                          completion_results->query.buffer(), &completion_results->results);
            *dir_sep = '/';
            prefix = dir_sep + 1;
        } else {
            // Root directory: "/u".
            cz::fs::files(cz::heap_allocator(),
                          completion_results->results_buffer_array.allocator(), "/",
                          &completion_results->results);
            prefix = dir_sep + 1;
        }
    } else {
        // Relative path without directories: "u".  Pretend they typed "./u" and load current
        // working directory (".").
        cz::fs::files(cz::heap_allocator(), completion_results->results_buffer_array.allocator(),
                      ".", &completion_results->results);
        prefix = completion_results->query;
    }
    std::sort(completion_results->results.start(), completion_results->results.end());

    size_t start;
    if (binary_search_string_prefix_start(completion_results->results, prefix, &start)) {
        size_t end = binary_search_string_prefix_end(completion_results->results, start, prefix);
        completion_results->results.set_len(end);
        completion_results->results.remove_range(0, start);
    } else {
        completion_results->results.set_len(0);
    }

    completion_results->state = Completion_Results::LOADED;
}

struct Buffer_Completion_Engine_Data {
    cz::Vector<cz::Str> all_results;
};

static void buffer_completion_engine_data_cleanup(void* _data) {
    Buffer_Completion_Engine_Data* data = (Buffer_Completion_Engine_Data*)_data;
    data->all_results.drop(cz::heap_allocator());
    free(data);
}

void buffer_completion_engine(Editor* editor, Completion_Results* completion_results) {
    ZoneScoped;

    if (!completion_results->data) {
        completion_results->data = calloc(1, sizeof(Buffer_Completion_Engine_Data));
        CZ_ASSERT(completion_results->data);
        completion_results->cleanup = buffer_completion_engine_data_cleanup;

        Buffer_Completion_Engine_Data* data =
            (Buffer_Completion_Engine_Data*)completion_results->data;

        data->all_results.reserve(cz::heap_allocator(), editor->buffers.len());
        for (size_t i = 0; i < editor->buffers.len(); ++i) {
            Buffer_Handle* handle = editor->buffers[i];
            Buffer* buffer = handle->lock();
            CZ_DEFER(handle->unlock());
            data->all_results.push(
                buffer->path.clone(completion_results->results_buffer_array.allocator()));
        }
    }

    Buffer_Completion_Engine_Data* data = (Buffer_Completion_Engine_Data*)completion_results->data;

    completion_results->results.set_len(0);
    size_t start;
    if (binary_search_string_prefix_start(data->all_results, completion_results->query, &start)) {
        size_t end =
            binary_search_string_prefix_end(data->all_results, start, completion_results->query);
        completion_results->results.reserve(cz::heap_allocator(), end - start);
        completion_results->results.append({data->all_results.elems() + start, end - start});
    }

    completion_results->state = Completion_Results::LOADED;
}

void no_completion_engine(Editor* _editor, Completion_Results* completion_results) {
    completion_results->state = Completion_Results::LOADED;
}

}
