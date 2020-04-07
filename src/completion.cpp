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

struct File_Completion_Engine_Data {
    cz::String directory;
    cz::String temp_result;
    cz::Vector<cz::Str> all_results;
    size_t offset;
};

static void file_completion_engine_data_cleanup(void* _data) {
    File_Completion_Engine_Data* data = (File_Completion_Engine_Data*)_data;
    data->directory.drop(cz::heap_allocator());
    data->temp_result.drop(cz::heap_allocator());
    data->all_results.drop(cz::heap_allocator());
    free(data);
}

static cz::Str get_directory_to_list(cz::String* directory, cz::Str query) {
    const char* dir_sep = query.rfind('/');
    cz::Str prefix;
    if (dir_sep) {
        if (dir_sep != query.buffer) {
            // Normal case: "./u" or "/a/b/c".
            size_t len = dir_sep - query.buffer;
            directory->reserve(cz::heap_allocator(), len + 1);
            directory->append({query.buffer, len});
            prefix = dir_sep + 1;
        } else {
            // Root directory: "/u".
            directory->reserve(cz::heap_allocator(), 2);
            directory->push('/');
            prefix = dir_sep + 1;
        }
    } else {
        // Relative path without directories: "u".  Pretend they typed "./u" and load current
        // working directory (".").
        directory->reserve(cz::heap_allocator(), 2);
        directory->push('.');
        prefix = query;
    }
    return prefix;
}

void file_completion_engine(Editor* _editor, Completion_Results* completion_results) {
    ZoneScoped;

    if (!completion_results->data) {
        completion_results->data = calloc(1, sizeof(File_Completion_Engine_Data));
        CZ_ASSERT(completion_results->data);
        completion_results->cleanup = file_completion_engine_data_cleanup;
    }

    File_Completion_Engine_Data* data = (File_Completion_Engine_Data*)completion_results->data;

    data->temp_result.set_len(0);
    cz::Str prefix = get_directory_to_list(&data->temp_result, completion_results->query);
    if (data->temp_result == data->directory) {
        // Track selected item
        completion_results->selected += data->offset;
    } else if (data->temp_result != data->directory) {
        std::swap(data->temp_result, data->directory);
        data->directory.null_terminate();

        completion_results->selected = 0;
        completion_results->results_buffer_array.clear();
        data->all_results.set_len(0);
        cz::fs::files(cz::heap_allocator(), completion_results->results_buffer_array.allocator(),
                      data->directory.buffer(), &data->all_results);
        std::sort(data->all_results.start(), data->all_results.end());
    }

    size_t start;
    completion_results->results.set_len(0);
    if (binary_search_string_prefix_start(data->all_results, prefix, &start)) {
        size_t end = binary_search_string_prefix_end(data->all_results, start, prefix);
        completion_results->results.reserve(cz::heap_allocator(), end - start);
        completion_results->results.append({data->all_results.elems() + start, end - start});

        data->offset = start;
        if (completion_results->selected >= start && completion_results->selected < end) {
            completion_results->selected -= start;
        } else {
            completion_results->selected = 0;
        }
    } else {
        data->offset = 0;
        completion_results->selected = 0;
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
