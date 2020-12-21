#include "completion.hpp"

#include <Tracy.hpp>
#include <algorithm>
#include <cz/defer.hpp>
#include <cz/fs/directory.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include <cz/sort.hpp>
#include <cz/util.hpp>
#include "buffer.hpp"
#include "editor.hpp"
#include "file.hpp"

namespace mag {

void Completion_Filter_Context::drop() {
    if (cleanup) {
        cleanup(data);
    }
    results.drop(cz::heap_allocator());
}

void Completion_Engine_Context::drop() {
    if (cleanup) {
        cleanup(data);
    }
    results.drop(cz::heap_allocator());
    results_buffer_array.drop();
    query.drop(cz::heap_allocator());
}

bool Completion_Cache::update(size_t changes_len) {
    if (change_index != changes_len) {
        change_index = changes_len;
        state = Completion_Cache::LOADING;
        engine_context.query.set_len(0);
        return true;
    }
    return false;
}

void Completion_Cache::set_engine(Completion_Engine new_engine) {
    engine = new_engine;
    state = Completion_Cache::INITIAL;
    if (engine_context.cleanup) {
        engine_context.cleanup(engine_context.data);
    }
    engine_context.cleanup = nullptr;
    engine_context.data = nullptr;
    engine_context.results_buffer_array.clear();
    engine_context.results.set_len(0);
}

void prefix_completion_filter(Editor* editor,
                             Completion_Filter_Context* context,
                             Completion_Engine_Context* engine_context,
                             cz::Str selected_result,
                             bool has_selected_result) {
    for (size_t i = 0; i < engine_context->results.len(); ++i) {
        cz::Str result = engine_context->results[i];
        if (result.starts_with(engine_context->query)) {
            if (has_selected_result && selected_result == result) {
                context->selected = context->results.len();
            }
            context->results.push(result);
        }
    }
}

void infix_completion_filter(Editor* editor,
                             Completion_Filter_Context* context,
                             Completion_Engine_Context* engine_context,
                             cz::Str selected_result,
                             bool has_selected_result) {
    for (size_t i = 0; i < engine_context->results.len(); ++i) {
        cz::Str result = engine_context->results[i];
        if (result.contains(engine_context->query)) {
            if (has_selected_result && selected_result == result) {
                context->selected = context->results.len();
            }
            context->results.push(result);
        }
    }
}

struct Wildcard_Pattern {
    bool wild_start = true;
    bool wild_end = true;
    cz::Vector<cz::Str> pieces;

    bool matches(cz::Str string) {
        size_t index = 0;
        for (size_t j = 0; j < pieces.len(); ++j) {
            cz::Str piece = pieces[j];
            if (j == 0 && !wild_start) {
                if (string.starts_with(piece)) {
                    index += piece.len;
                } else {
                    return false;
                }
            } else {
                const char* find = string.slice_start(index).find(piece);
                if (!find) {
                    return false;
                } else {
                    index = find - string.buffer + piece.len;
                }
            }
        }
        if (!wild_end && index < string.len) {
            return false;
        }
        return true;
    }
};

static Wildcard_Pattern parse_spaces_are_wildcards(cz::Str query) {
    Wildcard_Pattern pattern = {};

    if (query.len > 0) {
        size_t start = 0;
        if (query[start] == '^') {
            pattern.wild_start = false;
            ++start;
        }
        while (start < query.len && query[start] == ' ') {
            ++start;
        }

        size_t end = query.len;
        if (query[end - 1] == '$') {
            pattern.wild_end = false;
            --end;
        }
        while (end > 0 && query[end - 1] == ' ') {
            --end;
        }

        if (end < start) {
            query = "";
        } else {
            query = query.slice(start, end);
        }
    }

    while (true) {
        pattern.pieces.reserve(cz::heap_allocator(), 1);

        const char* space = query.find(' ');
        if (!space) {
            pattern.pieces.push(query);
            break;
        }

        pattern.pieces.push(query.slice_end(space));
        while (space < query.end() && *space == ' ') {
            ++space;
        }

        query = query.slice_start(space);
    }

    return pattern;
}

void spaces_are_wildcards_completion_filter(Editor* editor,
                                            Completion_Filter_Context* context,
                                            Completion_Engine_Context* engine_context,
                                            cz::Str selected_result,
                                            bool has_selected_result) {
    Wildcard_Pattern pattern = parse_spaces_are_wildcards(engine_context->query);
    CZ_DEFER(pattern.pieces.drop(cz::heap_allocator()));

    context->selected = 0;
    context->results.set_len(0);
    context->results.reserve(cz::heap_allocator(), engine_context->results.len());
    for (size_t i = 0; i < engine_context->results.len(); ++i) {
        cz::Str result = engine_context->results[i];
        if (pattern.matches(result)) {
            if (has_selected_result && selected_result == result) {
                context->selected = context->results.len();
            }
            context->results.push(result);
        }
    }
}

struct File_Completion_Engine_Data {
    cz::String directory;
    cz::String temp_result;
    void* file_time;
};

static void file_completion_engine_data_cleanup(void* _data) {
    File_Completion_Engine_Data* data = (File_Completion_Engine_Data*)_data;
    data->directory.drop(cz::heap_allocator());
    data->temp_result.drop(cz::heap_allocator());
    free(data->file_time);
    free(data);
}

static cz::Str get_directory_to_list(cz::String* directory, cz::Str query) {
    const char* dir_sep = query.rfind('/');
    if (dir_sep) {
        // Normal case: "./u" or "/a/b/c".
        size_t len = dir_sep - query.buffer + 1;
        directory->reserve(cz::heap_allocator(), len + 1);
        directory->append({query.buffer, len});
        return {query.buffer, len};
    } else {
        // Relative path without directories: "u".  Pretend they typed "./u" and load current
        // working directory (".").
        directory->reserve(cz::heap_allocator(), 3);
        directory->append("./");
        return {};
    }
}

bool file_completion_engine(Editor*, Completion_Engine_Context* context) {
    ZoneScoped;

    if (!context->data) {
        context->data = calloc(1, sizeof(File_Completion_Engine_Data));
        CZ_ASSERT(context->data);
        context->cleanup = file_completion_engine_data_cleanup;
    }

    File_Completion_Engine_Data* data = (File_Completion_Engine_Data*)context->data;

    data->temp_result.set_len(0);
    cz::Str prefix = get_directory_to_list(&data->temp_result, context->query);
    if (data->temp_result == data->directory) {
        if (!data->file_time || !is_out_of_date(data->directory.buffer(), data->file_time)) {
            // Directory has not changed so track the selected item.
            return false;
        }
    }

    std::swap(data->temp_result, data->directory);
    data->directory.null_terminate();

    // Directory has changed so load new results.
    context->results_buffer_array.clear();
    context->results.set_len(0);

    data->file_time = get_file_time(data->directory.buffer());

    do {
        cz::fs::DirectoryIterator iterator(cz::heap_allocator());
        if (iterator.create(data->directory.buffer()).is_err()) {
            break;
        }
        CZ_DEFER(iterator.destroy());
        while (!iterator.done()) {
            context->results.reserve(cz::heap_allocator(), 1);
            cz::String file = {};
            file.reserve(context->results_buffer_array.allocator(),
                         prefix.len + iterator.file().len);
            file.append(prefix);
            file.append(iterator.file());
            context->results.push(file);

            auto result = iterator.advance();
            if (result.is_err()) {
                break;
            }
        }
    } while (0);

    cz::sort(context->results);
    return true;
}

bool buffer_completion_engine(Editor* editor, Completion_Engine_Context* context) {
    ZoneScoped;

    if (context->results.len() > 0) {
        return false;
    }

    context->results_buffer_array.clear();
    context->results.set_len(0);
    context->results.reserve(cz::heap_allocator(), editor->buffers.len());
    for (size_t i = 0; i < editor->buffers.len(); ++i) {
        Buffer_Handle* handle = editor->buffers[i];
        Buffer* buffer = handle->lock();
        CZ_DEFER(handle->unlock());

        cz::String result = {};
        buffer->render_name(context->results_buffer_array.allocator(), &result);
        context->results.push(result);
    }
    return true;
}

bool no_completion_engine(Editor*, Completion_Engine_Context*) {
    return false;
}

bool run_command_for_completion_results(Completion_Engine_Context* context,
                                        cz::Slice<cz::Str> args,
                                        cz::Process_Options options) {
    if (context->results.len() > 0) {
        return false;
    }

    cz::Process process;
    cz::Input_File stdout_read;

    {
        if (!create_process_output_pipe(&options.std_out, &stdout_read)) {
            return false;
        }
        CZ_DEFER(options.std_out.close());

        if (!process.launch_program(args, &options)) {
            stdout_read.close();
            return false;
        }
    }
    CZ_DEFER(stdout_read.close());

    context->results_buffer_array.clear();
    context->results.set_len(0);

    char buffer[1024];
    cz::String result = {};
    cz::Carriage_Return_Carry carry;
    while (1) {
        int64_t len = stdout_read.read_text(buffer, sizeof(buffer), &carry);
        if (len > 0) {
            for (size_t offset = 0; offset < (size_t)len; ++offset) {
                const char* end = cz::Str{buffer + offset, len - offset}.find('\n');

                size_t rlen;
                if (end) {
                    rlen = end - buffer - offset;
                } else {
                    rlen = len - offset;
                }

                result.reserve(context->results_buffer_array.allocator(), rlen);
                result.append({buffer + offset, rlen});

                if (!end) {
                    break;
                }

                context->results.reserve(cz::heap_allocator(), 1);
                context->results.push(result);
                result = {};
                offset += rlen;
            }
        } else {
            break;
        }
    }

    process.join();

    cz::sort(context->results);
    return true;
}

}
