#include "gnu_global.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <Tracy.hpp>
#include <command_macros.hpp>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/format.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include <file.hpp>
#include <limits>
#include <movement.hpp>
#include <token.hpp>
#include "program_info.hpp"
#include "visible_region.hpp"

namespace mag {
namespace gnu_global {

template <class T>
static bool parse_number(cz::Str str, T* number) {
    *number = 0;
    for (size_t i = 0; i < str.len; ++i) {
        if (!cz::is_digit(str[i])) {
            return false;
        }

        if (*number > std::numeric_limits<T>::max() / 10) {
            return false;
        }
        *number *= 10;

        if (*number > std::numeric_limits<T>::max() - (str[i] - '0')) {
            return false;
        }
        *number += str[i] - '0';
    }
    return true;
}

struct Completion_Engine_Data {
    char* working_directory;
    Run_Command_For_Completion_Results runner;
};

bool completion_engine(Editor* editor, Completion_Engine_Context* context, bool is_initial_frame) {
    cz::Str args[] = {"global", "-c", context->query};
    Completion_Engine_Data* data = (Completion_Engine_Data*)context->data;
    cz::Process_Options options;
    options.working_directory = data->working_directory;
    return data->runner.iterate(context, args, options, is_initial_frame);
}

const char* lookup(const char* directory,
                   cz::Str query,
                   cz::Vector<Tag>* tags,
                   cz::String* buffer) {
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

        if (!create_process_output_pipe(&options.std_out, &std_out_read)) {
            return "Error: I/O operation failed";
        }
        options.std_err = options.std_out;
        CZ_DEFER(options.std_out.close());

        cz::Str rev_parse_args[] = {"global", "-at", query};
        if (!process.launch_program(rev_parse_args, &options)) {
            return "Couldn't launch `global`";
        }
    }

    *buffer = {};
    read_to_string(std_out_read, cz::heap_allocator(), buffer);

    int return_value = process.join();
    if (return_value != 0) {
        return "Failed to run `global`";
    }

    cz::Str rest = *buffer;
    while (1) {
        const char* lf = rest.find('\n');
        if (!lf) {
            break;
        }

        cz::Str line = rest.slice_end(lf);
        rest = rest.slice_start(lf + 1);

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

        Tag tag;
        tag.file_name = file_name_start;
        if (!parse_number(line.slice_start(line_number_start + 1), &tag.line)) {
            return "Invalid `global` output";
        }

        tags->reserve(cz::heap_allocator(), 1);
        tags->push(tag);
    }

    return nullptr;
}

static void open_tag(Editor* editor, Client* client, const Tag& tag) {
    ZoneScoped;

    {
        WITH_CONST_SELECTED_BUFFER(client);
        push_jump(window, client, buffer);
    }

    open_file(editor, client, tag.file_name);

    WITH_CONST_SELECTED_BUFFER(client);
    kill_extra_cursors(window, client);

    Contents_Iterator iterator = start_of_line_position(buffer->contents, tag.line);
    uint64_t old_point = window->cursors[window->selected_cursor].point;
    window->cursors[window->selected_cursor].point = iterator.position;

    if (iterator.position < window->start_position) {
        window->start_position = iterator.position;
    } else if (iterator.position > old_point) {
        Contents_Iterator ve = iterator;
        ve.retreat_to(window->start_position);
        forward_visual_line(window, buffer->mode, &ve, window->rows() - 1);
        if (iterator.position < ve.position) {
            center_in_window(window, buffer->mode, iterator);
        } else {
            window->start_position = iterator.position;
        }
    }
}

struct Tag_Completion_Engine_Data {
    cz::Vector<Tag> tags;
    cz::String buffer;
};

static void tag_completion_engine_cleanup(void* _data) {
    Tag_Completion_Engine_Data* data = (Tag_Completion_Engine_Data*)_data;
    data->tags.drop(cz::heap_allocator());
    data->buffer.drop(cz::heap_allocator());
    cz::heap_allocator().dealloc(data);
}

static bool tag_completion_engine(Editor* editor,
                                  Completion_Engine_Context* context,
                                  bool is_initial_frame) {
    if (!is_initial_frame) {
        return false;
    }

    Tag_Completion_Engine_Data* data = (Tag_Completion_Engine_Data*)context->data;

    context->results.set_len(0);
    context->results.reserve(data->tags.len());

    for (size_t i = 0; i < data->tags.len(); ++i) {
        cz::Allocator allocator = context->results_buffer_array.allocator();
        cz::String string = cz::format(allocator, data->tags[i].file_name, ':', data->tags[i].line);
        context->results.push(string);
    }

    return true;
}

static void prompt_open_tags_callback(Editor* editor, Client* client, cz::Str query, void* data) {
    const char* colon = query.rfind(':');
    if (!colon) {
        open_file(editor, client, query);
        return;
    }

    Tag tag;
    tag.file_name = query.slice_end(colon);
    tag.line = 0;
    if (!parse_number(query.slice_start(colon + 1), &tag.line)) {
        tag.file_name = query;
        tag.line = 0;
    }

    open_tag(editor, client, tag);
}

void prompt_open_tags(Editor* editor, Client* client, cz::Vector<Tag> tags, cz::String buffer) {
    if (tags.len() == 0) {
        tags.drop(cz::heap_allocator());
        buffer.drop(cz::heap_allocator());

        client->show_message(editor, "No global tag results");
        return;
    }

    if (tags.len() == 1) {
        CZ_DEFER({
            tags.drop(cz::heap_allocator());
            buffer.drop(cz::heap_allocator());
        });

        open_tag(editor, client, tags[0]);
        return;
    }

    Tag_Completion_Engine_Data* data = cz::heap_allocator().alloc<Tag_Completion_Engine_Data>();
    CZ_ASSERT(data);
    data->tags = tags;
    data->buffer = buffer;

    Dialog dialog = {};
    dialog.prompt = "Open tag: ";
    dialog.completion_engine = tag_completion_engine;
    dialog.response_callback = prompt_open_tags_callback;
    client->show_dialog(editor, dialog);

    if (client->mini_buffer_completion_cache.engine_context.data) {
        client->mini_buffer_completion_cache.engine_context.cleanup(
            client->mini_buffer_completion_cache.engine_context.data);
    }

    client->mini_buffer_completion_cache.engine_context.data = data;
    client->mini_buffer_completion_cache.engine_context.cleanup = tag_completion_engine_cleanup;
}

void lookup_and_prompt(Editor* editor, Client* client, const char* directory, cz::Str query) {
    cz::Vector<Tag> tags = {};
    cz::String str_buffer = {};

    const char* lookup_error = lookup(directory, query, &tags, &str_buffer);
    if (lookup_error) {
        tags.drop(cz::heap_allocator());
        str_buffer.drop(cz::heap_allocator());
        client->show_message(editor, lookup_error);
        return;
    }

    prompt_open_tags(editor, client, tags, str_buffer);
}

void command_lookup_at_point(Editor* editor, Command_Source source) {
    ZoneScoped;

    SSOStr query = {};
    CZ_DEFER(query.drop(cz::heap_allocator()));

    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    {
        WITH_SELECTED_BUFFER(source.client);

        Contents_Iterator iterator =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        Token token;
        if (!get_token_at_position(buffer, &iterator, &token)) {
            source.client->show_message(editor, "Cursor is not positioned at a token");
            return;
        }

        query = buffer->contents.slice(cz::heap_allocator(), iterator, token.end);

        directory = buffer->directory.clone_null_terminate(cz::heap_allocator());
    }

    lookup_and_prompt(editor, source.client, directory.buffer(), query.as_str());
}

void command_move_mouse_and_lookup_at_point(Editor* editor, Command_Source source) {
    if (!source.client->mouse.window || source.client->mouse.window->tag != Window::UNIFIED) {
        return;
    }

    source.client->selected_normal_window = (Window_Unified*)source.client->mouse.window;

    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        Contents_Iterator iterator =
            nearest_character(source.client->selected_normal_window, buffer,
                              source.client->mouse.row, source.client->mouse.column);
        kill_extra_cursors(window, source.client);
        window->cursors[0].point = iterator.position;
    }

    // This creates a race condition that the buffer hasn't modified
    // but I don't think that it will likely matter in practice.
    command_lookup_at_point(editor, source);
}

static void command_lookup_prompt_callback(Editor* editor,
                                           Client* client,
                                           cz::Str query,
                                           void* data) {
    ZoneScoped;

    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(client);
        directory = buffer->directory.clone_null_terminate(cz::heap_allocator());
    }

    lookup_and_prompt(editor, client, directory.buffer(), query);
}

void command_lookup_prompt(Editor* editor, Command_Source source) {
    ZoneScoped;

    char* directory;
    cz::String selected_region = {};
    CZ_DEFER(selected_region.drop(cz::heap_allocator()));
    {
        Window_Unified* window = source.client->selected_normal_window;
        WITH_CONST_WINDOW_BUFFER(window);

        directory = buffer->directory.clone_null_terminate(cz::heap_allocator()).buffer();

        get_selected_region(window, buffer, cz::heap_allocator(), &selected_region);
    }

    Dialog dialog = {};
    dialog.prompt = "Lookup: ";
    dialog.completion_engine = completion_engine;
    dialog.response_callback = command_lookup_prompt_callback;
    dialog.mini_buffer_contents = selected_region;
    source.client->show_dialog(editor, dialog);

    // If data wasn't cleared by show_dialog then it needs to be cleaned up now.
    auto data =
        (Completion_Engine_Data*)source.client->mini_buffer_completion_cache.engine_context.data;
    if (data) {
        CZ_DEBUG_ASSERT(source.client->mini_buffer_completion_cache.engine_context.cleanup);
        source.client->mini_buffer_completion_cache.engine_context.cleanup(data);
    }

    data = cz::heap_allocator().alloc<Completion_Engine_Data>();
    *data = {};
    data->working_directory = directory;

    source.client->mini_buffer_completion_cache.engine_context.data = data;
    source.client->mini_buffer_completion_cache.engine_context.cleanup = [](void* _data) {
        auto data = (Completion_Engine_Data*)_data;
        cz::heap_allocator().dealloc({data->working_directory, 0});
        data->runner.drop();
        cz::heap_allocator().dealloc(data);
    };
}

void command_complete_at_point(Editor* editor, Command_Source source) {
    ZoneScoped;

    char* directory;
    {
        WITH_SELECTED_BUFFER(source.client);

        Contents_Iterator iterator =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        Token token;
        if (!get_token_at_position(buffer, &iterator, &token)) {
            source.client->show_message(editor, "Cursor is not positioned at a token");
            return;
        }

        directory = buffer->directory.clone_null_terminate(cz::heap_allocator()).buffer();
    }

    Window_Unified* window = source.client->selected_window();

    // If data wasn't cleared by show_dialog then it needs to be cleaned up now.
    auto data = (Completion_Engine_Data*)window->completion_cache.engine_context.data;
    if (data) {
        CZ_DEBUG_ASSERT(window->completion_cache.engine_context.cleanup);
        window->completion_cache.engine_context.cleanup(data);
    }

    data = cz::heap_allocator().alloc<Completion_Engine_Data>();
    *data = {};
    data->working_directory = directory;

    window->start_completion(completion_engine);
    window->completion_cache.engine_context.data = data;
    window->completion_cache.engine_context.cleanup = [](void* _data) {
        auto data = (Completion_Engine_Data*)_data;
        cz::heap_allocator().dealloc({data->working_directory, 0});
        data->runner.drop();
        cz::heap_allocator().dealloc(data);
    };
}

void command_lookup_previous_command(Editor* editor, Command_Source source) {
    gnu_global::lookup_and_prompt(editor, source.client, mag_build_directory,
                                  source.previous_command.string);
}

}
}
