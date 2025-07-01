#include "completion.hpp"

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
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/command_macros.hpp"
#include "core/editor.hpp"
#include "core/file.hpp"
#include "core/program_info.hpp"

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
    result_prefix.drop(cz::heap_allocator());
    result_suffix.drop(cz::heap_allocator());
    results.drop();
    results_buffer_array.drop();
    query.drop(cz::heap_allocator());
}

void Completion_Engine_Context::reset() {
    if (cleanup) {
        cleanup(data);
    }
    cleanup = nullptr;
    data = nullptr;
    result_prefix.len = 0;
    result_suffix.len = 0;
    results_buffer_array.clear();
    results.len = 0;
}

void Completion_Engine_Context::parse_file_line_column_suffix() {
    cz::Str query_file;
    {
        uint64_t query_line, query_column;
        parse_file_arg_no_disk(query, &query_file, &query_line, &query_column);
    }

    cz::Str line_number_suffix = query.slice_start(query_file.len);
    result_suffix.len = 0;
    result_suffix.reserve(cz::heap_allocator(), line_number_suffix.len);
    result_suffix.append(line_number_suffix);

    query.len = query_file.len;
}

bool Completion_Cache::update(size_t changes_len) {
    if (change_index != changes_len) {
        change_index = changes_len;
        state = Completion_Cache::LOADING;
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
    engine_context.reset();
}

void prefix_completion_filter(Editor* editor,
                              Completion_Filter_Context* context,
                              Completion_Engine_Context* engine_context,
                              cz::Str selected_result,
                              bool has_selected_result) {
    for (size_t i = 0; i < engine_context->results.len; ++i) {
        cz::Str result = engine_context->results[i];
        if (result.starts_with(engine_context->query)) {
            if (has_selected_result && selected_result == result) {
                context->selected = context->results.len;
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
    for (size_t i = 0; i < engine_context->results.len; ++i) {
        cz::Str result = engine_context->results[i];
        if (result.contains(engine_context->query)) {
            if (has_selected_result && selected_result == result) {
                context->selected = context->results.len;
            }
            context->results.push(result);
        }
    }
}

static bool starts_with_uppercase_sticky(cz::Str string, cz::Str query) {
    if (query.len > string.len)
        return false;
    for (size_t i = 0; i < query.len; ++i) {
        if (cz::is_upper(query[i])) {
            if (string[i] != query[i]) {
                return false;
            }
        } else {
            if (cz::to_lower(string[i]) != query[i]) {
                return false;
            }
        }
    }
    return true;
}

static bool ends_with_uppercase_sticky(cz::Str string, cz::Str query) {
    if (query.len > string.len)
        return false;
    for (size_t i = 0; i < query.len; ++i) {
        if (cz::is_upper(query[i])) {
            if (string[string.len - query.len + i] != query[i]) {
                return false;
            }
        } else {
            if (cz::to_lower(string[string.len - query.len + i]) != query[i]) {
                return false;
            }
        }
    }
    return true;
}

static const char* find_uppercase_sticky(cz::Str string, cz::Str query) {
    for (size_t i = 0; i + query.len <= string.len; ++i) {
        if (starts_with_uppercase_sticky(string.slice_start(i), query)) {
            return string.buffer + i;
        }
    }
    return nullptr;
}

struct Wildcard_Pattern {
    bool wild_start = true;
    bool wild_start_component = true;
    bool wild_end = true;
    cz::Vector<cz::Str> pieces;

    bool matches(cz::Str string) {
        size_t index = 0;
        for (size_t j = 0; j < pieces.len; ++j) {
            cz::Str piece = pieces[j];

            if (j == 0 && (!wild_start || !wild_start_component)) {
                cz::Str p2 = piece;
                // We add a / to a wild_start_component so ignore it here.
                if (!wild_start_component) {
                    p2 = piece.slice_start(1);
                }

                if (starts_with_uppercase_sticky(string, p2)) {
                    index += p2.len;
                    continue;
                }

                // wild_start must be at the start; wild_start_component could be at the start
                // or after a / (note the piece includes the / so the general case works).
                if (!wild_start) {
                    return false;
                }
            }

            // If there are multiple parts of the string that match the last
            // piece and wild_end is set then we only care about the end.
            if (j + 1 == pieces.len && !wild_end) {
                return ends_with_uppercase_sticky(string, piece);
            }

            // Find the piece.
            const char* find = find_uppercase_sticky(string.slice_start(index), piece);
            if (!find) {
                return false;
            } else {
                index = find - string.buffer + piece.len;
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
    size_t end = query.len;

    // Recognize ^ at start -> first piece must be at the start of the string.
    if (start < query.len && query[start] == '^') {
        pattern.wild_start = false;
        ++start;
    }
    while (start < query.len && query[start] == ' ') {
        ++start;
    }

    // Recognize % at start -> first piece must either be at the start of the string or after a /.
    if (start < query.len && query[start] == '%') {
        pattern.wild_start_component = false;
        ++start;
    }
    while (start < query.len && query[start] == ' ') {
        ++start;
    }

    // Recognize $ at end -> last piece must be at the end of the string.
    if (end > start && query[end - 1] == '$') {
        pattern.wild_end = false;
        --end;
    }
    while (end > start && query[end - 1] == ' ') {
        --end;
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
                // `abc/%def` should be parsed as the piece `abc/def`.
                if (i + 1 < end && query[i + 1] == '%') {
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

    // Include '/' in the start of the piece so we can use it in `match`.
    if (!pattern.wild_start_component) {
        pattern.pieces[0].buffer--;
        pattern.pieces[0].len++;
        *(char*)pattern.pieces[0].buffer = '/';
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
    context->results.len = 0;
    context->results.reserve(engine_context->results.len);
    for (size_t i = 0; i < engine_context->results.len; ++i) {
        cz::Str result = engine_context->results[i];
        if (pattern.matches(result)) {
            if (has_selected_result && selected_result == result) {
                context->selected = context->results.len;
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
    size_t end = query.len;
    while (end-- > 0) {
        if (cz::path::is_dir_sep(query[end])) {
            found_dir_sep = true;
            break;
        }
    }

    if (found_dir_sep) {
        // Replace ~ with user home directory.
        if (query.starts_with("~/")) {
            if (user_home_path) {
                cz::Str home = user_home_path;
                size_t len = end;
                directory->reserve(cz::heap_allocator(), home.len + len + 1);
                directory->append(home);
                directory->append({query.buffer + 1, len});
                return {query.buffer, len + 1};
            }
        }

        // Normal case: "./u" or "/a/b/c".
        size_t len = end + 1;
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

    context->parse_file_line_column_suffix();

    data->temp_result.len = 0;
    cz::Str prefix = get_directory_to_list(&data->temp_result, context->query);
    if (data->temp_result == data->directory && prefix == context->result_prefix) {
        if (!data->has_file_time ||
            !check_out_of_date_and_update_file_time(data->directory.buffer, &data->file_time)) {
            // Directory has not changed so track the selected item.
            context->query.remove_range(0, prefix.len);
            return false;
        }
    }

    context->result_prefix.len = 0;
    context->result_prefix.reserve(cz::heap_allocator(), prefix.len);
    context->result_prefix.append(prefix);
    context->query.remove_range(0, prefix.len);

    cz::swap(data->temp_result, data->directory);
    data->directory.null_terminate();

    // Directory has changed so load new results.
    context->results_buffer_array.clear();
    context->results.len = 0;

    data->has_file_time = cz::get_file_time(data->directory.buffer, &data->file_time);

    do {
        cz::Heap_String query = {};
        CZ_DEFER(query.drop());
        query.reserve(data->directory.len);
        query.append(data->directory);

        cz::Directory_Iterator iterator;
        if (iterator.init(data->directory.buffer) <= 0) {
            break;
        }
        CZ_DEFER(iterator.drop());

        while (1) {
            cz::Str file = iterator.str_name();

            query.reserve(file.len + 1);
            query.append(file);
            query.null_terminate();
            bool add_slash = cz::file::is_directory(query.buffer);
            query.len = data->directory.len;

            cz::String result = {};
            result.reserve(context->results_buffer_array.allocator(), file.len + add_slash);
            result.append(file);
            if (add_slash) {
                result.push('/');
            }

            context->results.reserve(1);
            context->results.push(result);

            if (iterator.advance() <= 0) {
                // Ignore errors.
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

    if (!is_initial_frame && context->results.len > 0) {
        return false;
    }

    context->results_buffer_array.clear();
    context->results.len = 0;
    context->results.reserve(editor->buffers.len);
    for (size_t i = 0; i < editor->buffers.len; ++i) {
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

    if (!force_reload && context->results.len > 0 && !pimpl) {
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

        if (!data->process.launch_program(args, options)) {
            data->stdout_read.close();
            cz::heap_allocator().dealloc(data);
            return false;
        }

        pimpl = data;

        context->results_buffer_array.clear();
        context->results.len = 0;
    }

    char buffer[1024];
    bool done = false;
    while (1) {
        int64_t len = data->stdout_read.read_text(buffer, sizeof(buffer), &data->carry);
        if (len > 0) {
            cz::Str remaining = {buffer, (size_t)len};
            while (1) {
                cz::Str queued = remaining;
                bool split = remaining.split_excluding('\n', &queued, &remaining);

                data->result.reserve(context->results_buffer_array.allocator(), queued.len);
                data->result.append(queued);

                if (!split) {
                    break;
                }

                context->results.reserve(1);
                context->results.push(data->result);
                data->result = {};
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
