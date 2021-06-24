#include "completion.hpp"

#include <Tracy.hpp>
#include <algorithm>
#include <cz/defer.hpp>
#include <cz/directory.hpp>
#include <cz/file.hpp>
#include <cz/heap.hpp>
#include <cz/heap_string.hpp>
#include <cz/path.hpp>
#include <cz/process.hpp>
#include <cz/sort.hpp>
#include <cz/util.hpp>
#include "buffer.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "file.hpp"

namespace mag {

void Completion_Filter_Context::drop() {
    if (cleanup) {
        cleanup(data);
    }
    results.drop();
}

void Completion_Engine_Context::drop() {
    if (cleanup) {
        cleanup(data);
    }
    results.drop();
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
    if (engine == new_engine) {
        state = Completion_Cache::INITIAL;
        return;
    }

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
                if (string.starts_with_case_insensitive(piece)) {
                    index += piece.len;
                } else {
                    return false;
                }
            } else {
                const char* find = string.slice_start(index).find_case_insensitive(piece);
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

static Wildcard_Pattern parse_spaces_are_wildcards(cz::String& query) {
    ZoneScoped;

    Wildcard_Pattern pattern = {};

    size_t start = 0;
    size_t end = query.len();

    if (query.len() > 0) {
        if (query[start] == '^') {
            pattern.wild_start = false;
            ++start;
        }
        while (start < query.len() && query[start] == ' ') {
            ++start;
        }

        if (query[end - 1] == '$') {
            pattern.wild_end = false;
            --end;
        }
        while (end > 0 && query[end - 1] == ' ') {
            --end;
        }

        if (end < start) {
            start = end = 0;
        }
    }

    while (true) {
        pattern.pieces.reserve(cz::heap_allocator(), 1);

        // A piece ends in either a space, forward slash, or the end of the query.
        size_t i = start;
        for (; i < end; ++i) {
            if (query[i] == ' ') {
                break;
            }

            // Forward slashes break the piece but are still included in the piece.
            if (query[i] == '/') {
                // `abc/^def` should be parsed as the piece `abc/def`.
                if (i + 1 < end && query[i + 1] == '^') {
                    query.remove(i + 1);
                    --end;
                } else {
                    ++i;
                    break;
                }
            }
        }

        // Add the piece.
        pattern.pieces.push(query.slice(start, i));

        // If no spaces or forward slashes were found, continue.
        if (i == end) {
            break;
        }

        // Look for the start of the next piece.  Note that spaces are ignored whereas
        // forward slashes are still included in pieces so we don't jump over them.
        while (i < end && query[i] == ' ') {
            ++i;
        }

        // Advance.
        start = i;
    }

    return pattern;
}

void spaces_are_wildcards_completion_filter(Editor* editor,
                                            Completion_Filter_Context* context,
                                            Completion_Engine_Context* engine_context,
                                            cz::Str selected_result,
                                            bool has_selected_result) {
    ZoneScoped;

    cz::String query = engine_context->query.clone(cz::heap_allocator());
    CZ_DEFER(query.drop(cz::heap_allocator()));

    Wildcard_Pattern pattern = parse_spaces_are_wildcards(query);
    CZ_DEFER(pattern.pieces.drop(cz::heap_allocator()));

    context->selected = 0;
    context->results.set_len(0);
    context->results.reserve(engine_context->results.len());
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
    bool has_file_time;
    cz::File_Time file_time;
};

static void file_completion_engine_data_cleanup(void* _data) {
    File_Completion_Engine_Data* data = (File_Completion_Engine_Data*)_data;
    data->directory.drop(cz::heap_allocator());
    data->temp_result.drop(cz::heap_allocator());
    cz::heap_allocator().dealloc(data);
}

static cz::Str get_directory_to_list(cz::String* directory, cz::Str query) {
    bool found_dir_sep = false;
    size_t index = query.len;
    while (index > 0) {
        --index;
        if (cz::path::is_dir_sep(query[index])) {
            found_dir_sep = true;
            break;
        }
    }

    if (found_dir_sep) {
        // Replace ~ with user home directory.
        if (query.starts_with("~/")) {
            const char* user_home_path;
#ifdef _WIN32
            user_home_path = getenv("USERPROFILE");
#else
            user_home_path = getenv("HOME");
#endif

            if (user_home_path) {
                cz::Str home = user_home_path;
                size_t len = index;
                directory->reserve(cz::heap_allocator(), home.len + len + 1);
                directory->append(home);
                directory->append({query.buffer + 1, len});
                return {query.buffer, len + 1};
            }
        }

        // Normal case: "./u" or "/a/b/c".
        size_t len = index + 1;
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

bool file_completion_engine(Editor*, Completion_Engine_Context* context, bool) {
    ZoneScoped;

    File_Completion_Engine_Data* data = (File_Completion_Engine_Data*)context->data;
    if (!data) {
        data = cz::heap_allocator().alloc<File_Completion_Engine_Data>();
        CZ_ASSERT(data);
        *data = {};
        context->data = data;
        context->cleanup = file_completion_engine_data_cleanup;
    }

    data->temp_result.set_len(0);
    cz::Str prefix = get_directory_to_list(&data->temp_result, context->query);
    if (data->temp_result == data->directory) {
        if (!data->has_file_time ||
            !check_out_of_date_and_update_file_time(data->directory.buffer(), &data->file_time)) {
            // Directory has not changed so track the selected item.
            return false;
        }
    }

    std::swap(data->temp_result, data->directory);
    data->directory.null_terminate();

    // Directory has changed so load new results.
    context->results_buffer_array.clear();
    context->results.set_len(0);

    data->has_file_time = cz::get_file_time(data->directory.buffer(), &data->file_time);

    do {
        cz::Heap_String file = {};
        CZ_DEFER(file.drop());
        cz::Heap_String query = {};
        CZ_DEFER(query.drop());
        query.reserve(data->directory.len());
        query.append(data->directory);

        cz::Directory_Iterator iterator;
        if (iterator.init(data->directory.buffer(), cz::heap_allocator(), &file).is_err()) {
            break;
        }
        CZ_DEFER(iterator.drop());

        while (!iterator.done()) {
            query.reserve(file.len() + 1);
            query.append(file);
            query.null_terminate();
            bool add_slash = cz::file::is_directory(query.buffer());
            query.set_len(data->directory.len());

            cz::String result = {};
            result.reserve(context->results_buffer_array.allocator(),
                           prefix.len + file.len() + add_slash);
            result.append(prefix);
            result.append(file);
            if (add_slash) {
                result.push('/');
            }

            context->results.reserve(1);
            context->results.push(result);

            file.set_len(0);
            if (iterator.advance(cz::heap_allocator(), &file).is_err()) {
                break;
            }
        }
    } while (0);

    cz::sort(context->results);
    return true;
}

bool buffer_completion_engine(Editor* editor,
                              Completion_Engine_Context* context,
                              bool is_initial_frame) {
    ZoneScoped;

    if (!is_initial_frame && context->results.len() > 0) {
        return false;
    }

    context->results_buffer_array.clear();
    context->results.set_len(0);
    context->results.reserve(editor->buffers.len());
    for (size_t i = 0; i < editor->buffers.len(); ++i) {
        Buffer_Handle* handle = editor->buffers[i].get();
        WITH_CONST_BUFFER_HANDLE(handle);

        cz::String result = {};
        buffer->render_name(context->results_buffer_array.allocator(), &result);
        context->results.push(result);
    }
    return true;
}

bool no_completion_engine(Editor*, Completion_Engine_Context*, bool) {
    return false;
}

struct Run_Command_For_Completion_Results_Data {
    cz::Process process;
    cz::Input_File stdout_read;
    cz::String result;
    cz::Carriage_Return_Carry carry;
};

void Run_Command_For_Completion_Results::drop() {
    if (!pimpl) {
        return;
    }

    Run_Command_For_Completion_Results_Data* data = (Run_Command_For_Completion_Results_Data*)pimpl;
    data->stdout_read.close();
    data->process.kill();
    cz::heap_allocator().dealloc(data);
}

bool Run_Command_For_Completion_Results::iterate(Completion_Engine_Context* context,
                                                 cz::Slice<cz::Str> args,
                                                 cz::Process_Options options,
                                                 bool force_reload) {
    ZoneScoped;

    if (!force_reload && context->results.len() > 0 && !pimpl) {
        return false;
    }

    Run_Command_For_Completion_Results_Data* data = (Run_Command_For_Completion_Results_Data*)pimpl;
    if (!pimpl) {
        data = cz::heap_allocator().alloc<Run_Command_For_Completion_Results_Data>();
        *data = {};

        if (!create_process_output_pipe(&options.std_out, &data->stdout_read)) {
            cz::heap_allocator().dealloc(data);
            return false;
        }
        CZ_DEFER(options.std_out.close());
        data->stdout_read.set_non_blocking();

        if (!data->process.launch_program(args, &options)) {
            data->stdout_read.close();
            cz::heap_allocator().dealloc(data);
            return false;
        }

        pimpl = data;

        context->results_buffer_array.clear();
        context->results.set_len(0);
    }

    char buffer[1024];
    bool done = false;
    while (1) {
        int64_t len = data->stdout_read.read_text(buffer, sizeof(buffer), &data->carry);
        if (len > 0) {
            for (size_t offset = 0; offset < (size_t)len; ++offset) {
                const char* end = cz::Str{buffer + offset, len - offset}.find('\n');

                size_t rlen;
                if (end) {
                    rlen = end - buffer - offset;
                } else {
                    rlen = len - offset;
                }

                data->result.reserve(context->results_buffer_array.allocator(), rlen);
                data->result.append({buffer + offset, rlen});

                if (!end) {
                    break;
                }

                context->results.reserve(1);
                context->results.push(data->result);
                data->result = {};
                offset += rlen;
            }
        } else {
            done = len == 0;
            break;
        }
    }

    if (done) {
        data->stdout_read.close();
        data->process.join();
        cz::heap_allocator().dealloc(data);
        pimpl = nullptr;
    }

    cz::sort(context->results);
    return true;
}

}
