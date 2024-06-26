#include "generic.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <command_macros.hpp>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/find_file.hpp>
#include <cz/format.hpp>
#include <cz/heap.hpp>
#include <cz/parse.hpp>
#include <cz/process.hpp>
#include <file.hpp>
#include <limits>
#include <movement.hpp>
#include <token.hpp>
#include <tracy/Tracy.hpp>
#include "ctags.hpp"
#include "gnu_global.hpp"
#include "program_info.hpp"
#include "syntax/tokenize_path.hpp"
#include "visible_region.hpp"

namespace mag {
namespace tags {

///////////////////////////////////////////////////////////////////////////////
// Engine multiplex
///////////////////////////////////////////////////////////////////////////////

bool pick_engine(cz::Str directory, Engine* engine) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    path.reserve(cz::heap_allocator(), directory.len);

    path.append(directory);
    if (cz::find_file_up(cz::heap_allocator(), &path, "GTAGS")) {
        *engine = GNU_GLOBAL;
        return true;
    }
    path.len = 0;

    path.append(directory);
    if (cz::find_file_up(cz::heap_allocator(), &path, "TAGS")) {
        *engine = CTAGS;
        return true;
    }

    return false;
}

const char* lookup_symbol(const char* directory,
                          cz::Str query,
                          cz::Allocator allocator,
                          cz::Vector<Tag>* tags) {
    Engine engine;
    if (!pick_engine(directory, &engine))
        return "Error: no tags file found";

    switch (engine) {
    case GNU_GLOBAL:
        return gnu_global::lookup_symbol(directory, query, allocator, tags);
    case CTAGS:
        return ctags::lookup_symbol(directory, query, allocator, tags);
    }
    CZ_PANIC("Invalid engine type");
}

///////////////////////////////////////////////////////////////////////////////
// open_tag
///////////////////////////////////////////////////////////////////////////////

static void open_tag(Editor* editor, Client* client, const Tag& tag) {
    ZoneScoped;

    {
        WITH_CONST_SELECTED_NORMAL_BUFFER(client);
        push_jump(window, client, buffer);
    }

    open_file(editor, client, tag.file_name);

    WITH_CONST_SELECTED_NORMAL_BUFFER(client);
    kill_extra_cursors(window, client);

    Contents_Iterator iterator = start_of_line_position(buffer->contents, tag.line);
    uint64_t old_point = window->cursors[window->selected_cursor].point;
    window->cursors[window->selected_cursor].point = iterator.position;

    if (iterator.position < window->start_position) {
        window->start_position = iterator.position;
    } else if (iterator.position > old_point) {
        Contents_Iterator ve = iterator;
        ve.retreat_to(window->start_position);
        forward_visual_line(window, buffer->mode, editor->theme, &ve, window->rows() - 1);
        if (iterator.position < ve.position) {
            center_in_window(window, buffer->mode, editor->theme, iterator);
        } else {
            window->start_position = iterator.position;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// Tag completion engine
///////////////////////////////////////////////////////////////////////////////

struct Tag_Completion_Engine_Data {
    cz::Vector<Tag> tags;
    cz::Buffer_Array ba;
};

static void tag_completion_engine_cleanup(void* _data) {
    Tag_Completion_Engine_Data* data = (Tag_Completion_Engine_Data*)_data;
    data->tags.drop(cz::heap_allocator());
    data->ba.drop();
    cz::heap_allocator().dealloc(data);
}

static bool tag_completion_engine(Editor* editor,
                                  Completion_Engine_Context* context,
                                  bool is_initial_frame) {
    if (!is_initial_frame) {
        return false;
    }

    Tag_Completion_Engine_Data* data = (Tag_Completion_Engine_Data*)context->data;

    context->results.len = 0;
    context->results.reserve(data->tags.len);

    for (size_t i = 0; i < data->tags.len; ++i) {
        cz::Allocator allocator = context->results_buffer_array.allocator();
        cz::String string = cz::format(allocator, data->tags[i].file_name, ':', data->tags[i].line);
        context->results.push(string);
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// prompt_open_tags
///////////////////////////////////////////////////////////////////////////////

static void prompt_open_tags_callback(Editor* editor, Client* client, cz::Str query, void* data) {
    const char* colon = query.rfind(':');
    if (!colon) {
        open_file(editor, client, query);
        return;
    }

    Tag tag;
    tag.file_name = query.slice_end(colon);
    tag.line = 0;
    if (cz::parse(query.slice_start(colon + 1), &tag.line) <= 0) {
        tag.file_name = query;
        tag.line = 0;
    }

    open_tag(editor, client, tag);
}

void prompt_open_tags(Editor* editor, Client* client, cz::Vector<Tag> tags, cz::Buffer_Array ba) {
    if (tags.len == 0) {
        tags.drop(cz::heap_allocator());
        ba.drop();

        client->show_message("No global tag results");
        return;
    }

    if (tags.len == 1) {
        CZ_DEFER({
            tags.drop(cz::heap_allocator());
            ba.drop();
        });

        open_tag(editor, client, tags[0]);
        return;
    }

    // Note: keep ba alive because tags live in it.
    Tag_Completion_Engine_Data* data = cz::heap_allocator().alloc<Tag_Completion_Engine_Data>();
    CZ_ASSERT(data);
    data->tags = tags;
    data->ba = ba;

    Dialog dialog = {};
    dialog.prompt = "Open tag: ";
    dialog.completion_engine = tag_completion_engine;
    dialog.response_callback = prompt_open_tags_callback;
    dialog.next_token = syntax::path_next_token;
    client->show_dialog(dialog);

    client->mini_buffer_completion_cache.engine_context.reset();
    client->mini_buffer_completion_cache.engine_context.data = data;
    client->mini_buffer_completion_cache.engine_context.cleanup = tag_completion_engine_cleanup;
}

///////////////////////////////////////////////////////////////////////////////
// lookup_and_prompt
///////////////////////////////////////////////////////////////////////////////

void lookup_and_prompt(Editor* editor, Client* client, const char* directory, cz::Str query) {
    cz::Vector<Tag> tags = {};
    cz::Buffer_Array ba;
    ba.init();

    const char* lookup_error = lookup_symbol(directory, query, ba.allocator(), &tags);
    if (lookup_error) {
        tags.drop(cz::heap_allocator());
        ba.drop();
        client->show_message(lookup_error);
        return;
    }

    prompt_open_tags(editor, client, tags, ba);
}

///////////////////////////////////////////////////////////////////////////////
// Lookup commands
///////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_lookup_at_point);
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
            source.client->show_message("Cursor is not positioned at a token");
            return;
        }

        query = buffer->contents.slice(cz::heap_allocator(), iterator, token.end);

        directory = buffer->directory.clone_null_terminate(cz::heap_allocator());
    }

    lookup_and_prompt(editor, source.client, directory.buffer, query.as_str());
}

REGISTER_COMMAND(command_move_mouse_and_lookup_at_point);
void command_move_mouse_and_lookup_at_point(Editor* editor, Command_Source source) {
    if (!source.client->mouse.window || source.client->mouse.window->tag != Window::UNIFIED) {
        return;
    }

    source.client->selected_normal_window = (Window_Unified*)source.client->mouse.window;

    {
        WITH_CONST_SELECTED_NORMAL_BUFFER(source.client);
        Contents_Iterator iterator =
            nearest_character(window, buffer, editor->theme, source.client->mouse.window_row,
                              source.client->mouse.window_column);
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

    lookup_and_prompt(editor, client, directory.buffer, query);
}

REGISTER_COMMAND(command_lookup_prompt);
void command_lookup_prompt(Editor* editor, Command_Source source) {
    ZoneScoped;

    Engine engine;
    char* directory;
    cz::String selected_region = {};
    CZ_DEFER(selected_region.drop(cz::heap_allocator()));
    {
        Window_Unified* window = source.client->selected_normal_window;
        WITH_CONST_WINDOW_BUFFER(window);

        if (!pick_engine(buffer->directory, &engine)){
            source.client->show_message("Error: no tags file found");
            return;
        }

        directory = buffer->directory.clone_null_terminate(cz::heap_allocator()).buffer;

        get_selected_region(window, buffer, cz::heap_allocator(), &selected_region);
    }

    Dialog dialog = {};
    dialog.prompt = "Lookup: ";
    dialog.response_callback = command_lookup_prompt_callback;
    dialog.mini_buffer_contents = selected_region;

    switch (engine) {
    case GNU_GLOBAL:
        dialog.completion_engine = gnu_global::completion_engine;
        break;
    case CTAGS:
        dialog.completion_engine = ctags::completion_engine;
        break;
    }

    source.client->show_dialog(dialog);

    // If data wasn't cleared by show_dialog then it needs to be cleaned up now.
    source.client->mini_buffer_completion_cache.engine_context.reset();

    switch (engine) {
    case GNU_GLOBAL:
        gnu_global::init_completion_engine_context(
            &source.client->mini_buffer_completion_cache.engine_context, directory);
        break;
    case CTAGS:
        ctags::init_completion_engine_context(
            &source.client->mini_buffer_completion_cache.engine_context, directory);
        break;
    }
}

REGISTER_COMMAND(command_lookup_previous_command);
void command_lookup_previous_command(Editor* editor, Command_Source source) {
    lookup_and_prompt(editor, source.client, mag_build_directory, source.previous_command.string);
}

///////////////////////////////////////////////////////////////////////////////
// Completion commands
///////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_complete_at_point);
void command_complete_at_point(Editor* editor, Command_Source source) {
    ZoneScoped;

    Engine engine;
    char* directory;
    {
        WITH_SELECTED_BUFFER(source.client);

        Contents_Iterator iterator =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        Token token;
        if (!get_token_at_position(buffer, &iterator, &token)) {
            source.client->show_message("Cursor is not positioned at a token");
            return;
        }

        if (!pick_engine(buffer->directory, &engine)) {
            source.client->show_message("Error: no tags file found");
            return;
        }

        directory = buffer->directory.clone_null_terminate(cz::heap_allocator()).buffer;
    }

    Window_Unified* window = source.client->selected_window();
    window->completion_cache.engine_context.reset();

    switch (engine) {
    case GNU_GLOBAL:
        window->start_completion(gnu_global::completion_engine);
        return gnu_global::init_completion_engine_context(&window->completion_cache.engine_context,
                                                          directory);
    case CTAGS:
        window->start_completion(ctags::completion_engine);
        return ctags::init_completion_engine_context(&window->completion_cache.engine_context,
                                                     directory);
    }
}

}
}
