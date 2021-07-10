#include "help_commands.hpp"

#include <cz/char_type.hpp>
#include <cz/dedup.hpp>
#include <cz/sort.hpp>
#include <cz/stringify.hpp>
#include "command_macros.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "gnu_global/gnu_global.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "program_info.hpp"
#include "transaction.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

static void add_key_map(Contents* contents, cz::String* prefix, const Key_Map& key_map) {
    prefix->reserve(cz::heap_allocator(), stringify_key_max_size + 1);
    for (size_t i = 0; i < key_map.bindings.len(); ++i) {
        auto& binding = key_map.bindings[i];
        size_t old_len = prefix->len();
        stringify_key(prefix, binding.key);

        if (binding.is_command) {
            contents->append(*prefix);
            // Align with column 20 by adding spaces to 19 and then a mandatory space.
            for (size_t i = prefix->len(); i < 19; ++i) {
                contents->append(" ");
            }
            contents->append(" ");
            contents->append(binding.v.command.string);
            contents->append("\n");
        } else {
            prefix->push(' ');
            add_key_map(contents, prefix, *binding.v.map);
        }

        prefix->set_len(old_len);
    }
}

void command_dump_key_map(Editor* editor, Command_Source source) {
    cz::Arc<Buffer_Handle> handle;
    if (!find_temp_buffer(editor, source.client, "key map", {mag_build_directory}, &handle)) {
        handle = editor->create_temp_buffer("key map", {mag_build_directory});
    }

    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        push_jump(window, source.client, buffer);
    }

    WITH_BUFFER_HANDLE(handle);
    buffer->contents.remove(0, buffer->contents.len);

    cz::String prefix = {};
    CZ_DEFER(prefix.drop(cz::heap_allocator()));

    add_key_map(&buffer->contents, &prefix, editor->key_map);

    source.client->set_selected_buffer(handle->id);
}

/// Append all commands in the key map to the results.  All strings are allocated with `allocator`.
static void get_command_names(cz::Vector<cz::Str>* results,
                              cz::Allocator allocator,
                              const Key_Map& key_map) {
    for (size_t i = 0; i < key_map.bindings.len(); ++i) {
        if (key_map.bindings[i].is_command) {
            results->reserve(cz::heap_allocator(), 1);
            results->push(cz::Str{key_map.bindings[i].v.command.string}.clone(allocator));
        } else {
            get_command_names(results, allocator, *key_map.bindings[i].v.map);
        }
    }
}

static bool command_completion_engine(Editor* editor,
                                      Completion_Engine_Context* context,
                                      bool is_initial_frame) {
    ZoneScoped;

    if (!is_initial_frame && context->results.len() > 0) {
        return false;
    }

    context->results_buffer_array.clear();
    context->results.set_len(0);
    context->results.reserve(128);

    cz::Allocator allocator = context->results_buffer_array.allocator();

    get_command_names(&context->results, allocator, editor->key_map);

    context->results.reserve(editor->misc_commands.len());
    for (size_t i = 0; i < editor->misc_commands.len(); ++i) {
        context->results.push(cz::Str{editor->misc_commands[i].string}.clone(allocator));
    }

    cz::sort(context->results);
    cz::dedup(&context->results);

    return true;
}

static Command_Function find_command(const Key_Map& key_map, cz::Str str) {
    for (size_t i = 0; i < key_map.bindings.len(); ++i) {
        if (key_map.bindings[i].is_command) {
            if (str == key_map.bindings[i].v.command.string) {
                return key_map.bindings[i].v.command.function;
            }
        } else {
            Command_Function command = find_command(*key_map.bindings[i].v.map, str);
            if (command) {
                return command;
            }
        }
    }

    return nullptr;
}

static void command_run_command_by_name_callback(Editor* editor,
                                                 Client* client,
                                                 cz::Str str,
                                                 void* data) {
    Command_Function command;

    {
        Buffer_Id* buffer_id = (Buffer_Id*)data;
        WITH_BUFFER(*buffer_id);
        command = find_command(buffer->mode.key_map, str);
    }

    if (!command) {
        command = find_command(editor->key_map, str);
    }

    if (!command) {
        for (size_t i = 0; i < editor->misc_commands.len(); ++i) {
            if (str == editor->misc_commands[i].string) {
                command = editor->misc_commands[i].function;
                break;
            }
        }
    }

    if (!command) {
        client->show_message(editor, "No command found by that name");
        return;
    }

    Command_Source source;
    source.client = client;
    source.keys = {};
    source.previous_command.function = command_run_command_by_name;
    source.previous_command.string = "command_run_command_by_name";
    command(editor, source);
}

void command_run_command_by_name(Editor* editor, Command_Source source) {
    Buffer_Id* buffer_id = cz::heap_allocator().alloc<Buffer_Id>();
    CZ_ASSERT(buffer_id);
    *buffer_id = source.client->selected_window()->id;

    Dialog dialog = {};
    dialog.prompt = "Run command: ";
    dialog.completion_engine = command_completion_engine;
    dialog.response_callback = command_run_command_by_name_callback;
    dialog.response_callback_data = buffer_id;
    source.client->show_dialog(editor, dialog);
}

void command_go_to_key_map_binding(Editor* editor, Command_Source source) {
    cz::String query = {};
    CZ_DEFER(query.drop(cz::heap_allocator()));

    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_BUFFER(source.client);

        Contents_Iterator it =
            buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
        end_of_line(&it);
        Contents_Iterator end = it;

        // Backward through identifier characters.
        do {
            if (it.at_bob()) {
                return;
            }

            it.retreat();
        } while (cz::is_alnum(it.get()) || it.get() == '_');
        it.advance();

        // Nothing at point.
        if (end.position <= it.position) {
            return;
        }

        query.reserve(cz::heap_allocator(), end.position - it.position);
        buffer->contents.slice_into(it, end.position, &query);

        directory = buffer->directory.clone_null_terminate(cz::heap_allocator());
    }

    gnu_global::lookup_and_prompt(editor, source.client, directory.buffer(), query);
}

}
}
