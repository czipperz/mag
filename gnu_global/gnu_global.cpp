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

const char* lookup(const char* directory, cz::Str query, Tag* tag) {
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

        cz::Str rev_parse_args[] = {"global", "-at", query};
        if (!process.launch_program(cz::slice(rev_parse_args), &options)) {
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

    int scan_ret = sscanf(line_number_start + 1, "%" PRIu64, &tag->line);
    if (scan_ret == EOF || scan_ret < 1) {
        return "Invalid `global` output";
    }

    tag->file_name = file_name_start;
    tag->buffer = buffer;
    buffer = {};

    return nullptr;
}

static void open_tag(Editor* editor, Client* client, const Tag& tag) {
    open_file(editor, client, tag.file_name);

    WITH_SELECTED_BUFFER(client);
    kill_extra_cursors(window, client);

    Contents_Iterator iterator = start_of_line_position(buffer->contents, tag.line);
    window->cursors[0].point = iterator.position;
}

void command_lookup_at_point(Editor* editor, Command_Source source) {
    Tag tag;
    {
        WITH_SELECTED_BUFFER(source.client);

        Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);
        Token token;
        if (!get_token_at_position(buffer, &iterator, &token)) {
            source.client->show_message("Cursor is not positioned at a token");
            return;
        }

        char* query = (char*)malloc(token.end - token.start + 1);
        CZ_ASSERT(query);
        CZ_DEFER(free(query));
        buffer->contents.slice_into(iterator, token.end, query);
        query[token.end - token.start] = '\0';

        const char* lookup_error = lookup(buffer->directory.buffer(), query, &tag);
        if (lookup_error) {
            source.client->show_message(lookup_error);
            return;
        }

        push_jump(window, source.client, handle->id, buffer);
    }

    CZ_DEFER(tag.buffer.drop(cz::heap_allocator()));

    open_tag(editor, source.client, tag);
}

static void command_lookup_prompt_callback(Editor* editor,
                                           Client* client,
                                           cz::Str query,
                                           void* data) {
    Tag tag;
    {
        WITH_SELECTED_BUFFER(client);
        const char* lookup_error = lookup(buffer->directory.buffer(), query, &tag);
        if (lookup_error) {
            client->show_message(lookup_error);
            return;
        }

        push_jump(window, client, handle->id, buffer);
    }
    CZ_DEFER(tag.buffer.drop(cz::heap_allocator()));

    open_tag(editor, client, tag);
}

void command_lookup_prompt(Editor* editor, Command_Source source) {
    source.client->show_dialog(editor, "Lookup: ", no_completion_engine,
                               command_lookup_prompt_callback, nullptr);
}

static void gnu_global_completion_engine(Editor* editor, Completion_Engine_Context* context) {
    cz::Str args[] = {"global", "-c", context->query};
    cz::Process_Options options;
    options.working_directory = (const char*)context->data;
    run_command_for_completion_results(context, cz::slice(args), options);
}

void command_complete_at_point(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);
    Token token;
    if (!get_token_at_position(buffer, &iterator, &token)) {
        source.client->show_message("Cursor is not positioned at a token");
        return;
    }

    char* directory = (char*)malloc(buffer->directory.len() + 1);
    CZ_ASSERT(directory);
    memcpy(directory, buffer->directory.buffer(), buffer->directory.len());
    directory[buffer->directory.len()] = '\0';

    window->start_completion(gnu_global_completion_engine);
    window->completion_cache.engine_context.data = directory;
    window->completion_cache.engine_context.cleanup = [](void* directory) { free(directory); };
}

}
}
