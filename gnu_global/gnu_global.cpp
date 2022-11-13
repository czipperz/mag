#include "gnu_global.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <Tracy.hpp>
#include <command_macros.hpp>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/format.hpp>
#include <cz/heap.hpp>
#include <cz/parse.hpp>
#include <cz/process.hpp>
#include <file.hpp>
#include <limits>
#include <movement.hpp>
#include <token.hpp>
#include "program_info.hpp"
#include "syntax/tokenize_path.hpp"
#include "visible_region.hpp"

namespace mag {
namespace gnu_global {

struct Completion_Engine_Data {
    char* working_directory;
    Run_Command_For_Completion_Results runner;
};

void init_completion_engine_context(Completion_Engine_Context* engine_context, char* directory) {
    Completion_Engine_Data* data = cz::heap_allocator().alloc<Completion_Engine_Data>();
    *data = {};
    data->working_directory = directory;

    engine_context->data = data;
    engine_context->cleanup = [](void* _data) {
        auto data = (Completion_Engine_Data*)_data;
        cz::heap_allocator().dealloc({data->working_directory, 0});
        data->runner.drop();
        cz::heap_allocator().dealloc(data);
    };
}

bool completion_engine(Editor* editor, Completion_Engine_Context* context, bool is_initial_frame) {
    cz::Str args[] = {"global", "-c", context->query};
    Completion_Engine_Data* data = (Completion_Engine_Data*)context->data;
    cz::Process_Options options;
    options.working_directory = data->working_directory;
#ifdef _WIN32
    options.hide_window = true;
#endif
    return data->runner.iterate(context, args, options, is_initial_frame);
}

const char* lookup_symbol(const char* directory,
                          cz::Str query,
                          cz::Allocator allocator,
                          cz::Vector<tags::Tag>* tags) {
    ZoneScoped;

    // GNU Global cannot deal with namespaced lookups so strip it now.
    const char* ns = query.rfind("::");
    if (ns) {
        query = query.slice_start(ns + 2);
    }

    cz::Input_File std_out_read;
    CZ_DEFER(std_out_read.close());

    cz::Process process;
    {
        cz::Process_Options options;
        options.working_directory = directory;
#ifdef _WIN32
        options.hide_window = true;
#endif

        if (!create_process_output_pipe(&options.std_out, &std_out_read)) {
            return "Error: I/O operation failed";
        }
        options.std_err = options.std_out;
        CZ_DEFER(options.std_out.close());

        cz::Str rev_parse_args[] = {"global", "-at", query};
        if (!process.launch_program(rev_parse_args, options)) {
            return "Couldn't launch `global`";
        }
    }

    cz::String buffer = {};
    read_to_string(std_out_read, allocator, &buffer);

    int return_value = process.join();
    if (return_value != 0) {
        return "Failed to run `global`";
    }

    cz::Str rest = buffer;
    while (1) {
        cz::Str line;
        if (!rest.split_excluding('\n', &line, &rest))
            break;

        const char* file_name_start = line.find('\t');
        if (!file_name_start || file_name_start + 1 >= line.end()) {
            return "Invalid `global` output";
        }

        ++file_name_start;

        const char* line_number_start = line.slice_start(file_name_start).find('\t');
        if (!line_number_start || line_number_start + 2 > line.end()) {
            return "Invalid `global` output";
        }

        *(char*)line_number_start = '\0';

        tags::Tag tag;
        tag.file_name = file_name_start;
        if (cz::parse(line.slice_start(line_number_start + 1), &tag.line) <= 0) {
            return "Invalid `global` output";
        }

        tags->reserve(cz::heap_allocator(), 1);
        tags->push(tag);
    }

    return nullptr;
}

}
}
