#include "gnu_global.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <command_macros.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include <file.hpp>
#include <movement.hpp>
#include <token.hpp>

namespace mag {
namespace gnu_global {

const char* lookup(const char* directory, const char* tag, Reference* reference) {
    cz::Input_File std_out_read;
    CZ_DEFER(std_out_read.close());

    cz::Process process;
    {
        cz::Process_Options options;
        options.working_directory = directory;

        if (!create_process_output_pipe(&options.std_out, &std_out_read)) {
            return "Error: I/O operation failed";
        }
        options.std_err = options.std_out;
        CZ_DEFER(options.std_out.close());

        const char* rev_parse_args[] = {"global", "-at", tag, nullptr};
        if (!process.launch_program(rev_parse_args, &options)) {
            return "Couldn't launch `global`";
        }
    }

    cz::String buffer = {};
    CZ_DEFER(buffer.drop(cz::heap_allocator()));
    read_to_string(std_out_read, cz::heap_allocator(), &buffer);

    int return_value = process.join();
    if (return_value != 0) {
        return "Failed to run `global`";
    }

    const char* file_name_start = buffer.find('\t');
    if (!file_name_start || file_name_start + 1 >= buffer.end()) {
        return "Invalid `global` output";
    }

    ++file_name_start;

    const char* line_number_start = buffer.slice_start(file_name_start).find('\t');
    if (!line_number_start || line_number_start + 2 >= buffer.end()) {
        return "Invalid `global` output";
    }

    const char* line_number_end = buffer.slice_start(line_number_start + 1).find('\n');
    if (!line_number_end) {
        return "Invalid `global` output";
    }
    *(char*)line_number_start = '\0';
    *(char*)line_number_end = '\0';

    int scan_ret = sscanf(line_number_start + 1, "%" PRIu64, &reference->line);
    if (scan_ret == EOF || scan_ret < 1) {
        return "Invalid `global` output";
    }

    reference->file_name = file_name_start;
    reference->buffer = buffer;
    buffer = {};

    return nullptr;
}

void command_lookup(Editor* editor, Command_Source source) {
    Reference reference;
    {
        WITH_SELECTED_BUFFER(source.client);

        Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);
        uint64_t state;
        Token token;
        if (!get_token_at_position(buffer, &iterator, &state, &token)) {
            source.client->show_message("Cursor is not positioned at a token");
            return;
        }

        iterator.retreat_to(token.start);
        char* tag = (char*)malloc(token.end - token.start + 1);
        CZ_ASSERT(tag);
        CZ_DEFER(free(tag));
        buffer->contents.slice_into(iterator, token.end, tag);
        tag[token.end - token.start] = '\0';

        const char* lookup_error = lookup(buffer->directory.buffer(), tag, &reference);
        if (lookup_error) {
            source.client->show_message(lookup_error);
            return;
        }

        push_jump(window, source.client, handle->id, buffer);
    }

    CZ_DEFER(reference.buffer.drop(cz::heap_allocator()));

    open_file(editor, source.client, reference.file_name);

    {
        WITH_SELECTED_BUFFER(source.client);
        kill_extra_cursors(window, source.client);

        Contents_Iterator iterator = start_of_line_position(buffer->contents, reference.line);
        window->cursors[0].point = iterator.position;
    }
}

}
}
