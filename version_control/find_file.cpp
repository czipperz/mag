#include "find_file.hpp"

#include <cz/defer.hpp>
#include <cz/directory.hpp>
#include <cz/file.hpp>
#include <cz/heap.hpp>
#include <cz/path.hpp>
#include <cz/process.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "ignore.hpp"
#include "version_control.hpp"

namespace mag {
namespace version_control {

struct Find_File_Completion_Engine_Data {
    size_t path_initial_len;
    cz::String path;

    cz::Vector<cz::Directory_Iterator> directories;
    bool finished;

    bool already_started;
    Ignore_Rules ignore_rules;
};

static bool find_file_completion_engine(Editor*,
                                        Completion_Engine_Context* context,
                                        bool is_initial_frame) {
    Find_File_Completion_Engine_Data* data = (Find_File_Completion_Engine_Data*)context->data;
    if (is_initial_frame) {
        data->path.set_len(data->path_initial_len);
        data->path.null_terminate();

        data->finished = false;

        for (size_t i = data->directories.len(); i-- > 0;) {
            (void)data->directories[i].drop();
        }
        data->directories.set_len(0);

        if (!data->already_started) {
            data->already_started = true;
            find_ignore_rules(data->path.buffer(), &data->ignore_rules);
        }

        context->results_buffer_array.clear();
        context->results.set_len(0);
    }

    if (data->finished) {
        return false;
    }

    const size_t max_depth = 32;
    const size_t max_iterations = 128;

    for (size_t i = 0; i < max_iterations; ++i) {
        if (data->directories.len() == 0) {
            // First time: create the first iterator and get the first file.
            cz::Directory_Iterator iterator;
            if (iterator.init(data->path.buffer(), cz::heap_allocator(), &data->path).is_err()) {
                data->finished = true;
                return false;
            }

            data->directories.reserve(cz::heap_allocator(), max_depth);
            data->directories.push(iterator);
        } else {
            // Find the next file.
            while (1) {
                cz::Directory_Iterator& iterator = data->directories.last();
                if (iterator.advance(cz::heap_allocator(), &data->path).is_err() ||
                    iterator.done()) {
                    // If this directory is dead then go to the parent.
                    (void)iterator.drop();
                    data->directories.pop();

                    data->path.pop();
                    cz::path::pop_name(&data->path);

                    if (data->directories.len() == 0) {
                        data->finished = true;
                        return false;
                    }

                    continue;
                }
                break;
            }
        }

        // Handle ignored files.
        if (file_matches(data->ignore_rules, data->path.slice_start(data->path_initial_len - 1))) {
            cz::path::pop_name(&data->path);
            continue;
        }

        if (cz::file::is_directory(data->path.buffer())) {
            // Recursively descend into directories.
            do {
                // If we go too deep then just stop.
                if (data->directories.remaining() == 0) {
                    break;
                }

                data->path.reserve(cz::heap_allocator(), 2);
                data->path.push('/');
                data->path.null_terminate();

                cz::Directory_Iterator iterator;
                if (iterator.init(data->path.buffer(), cz::heap_allocator(), &data->path)
                        .is_err()) {
                    data->path.pop();
                    break;
                }
                data->directories.push(iterator);
            } while (cz::file::is_directory(data->path.buffer()));

            // We now have a valid path so fall through.
        }

        context->results.reserve(1);
        context->results.push(data->path.slice_start(data->path_initial_len)
                                  .duplicate(context->results_buffer_array.allocator()));
        cz::path::pop_name(&data->path);
    }

    return true;
}

static void command_find_file_response(Editor* editor, Client* client, cz::Str file, void* data) {
    cz::Str directory = (char*)data;

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    path.reserve(cz::heap_allocator(), directory.len + file.len + 1);
    path.append(directory);
    path.append(file);
    path.null_terminate();

    open_file(editor, client, path);
}

void command_find_file(Editor* editor, Command_Source source) {
    cz::String top_level_path = {};
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!get_root_directory(editor, source.client, buffer->directory.buffer(),
                                cz::heap_allocator(), &top_level_path)) {
            top_level_path.drop(cz::heap_allocator());
            return;
        }
    }
    top_level_path.reserve(cz::heap_allocator(), 2);
    top_level_path.push('/');
    top_level_path.null_terminate();

    source.client->show_dialog(editor, "Version Control Find File: ", find_file_completion_engine,
                               command_find_file_response, top_level_path.buffer());

    auto data = cz::heap_allocator().alloc<Find_File_Completion_Engine_Data>();
    *data = {};
    data->path = top_level_path.clone_null_terminate(cz::heap_allocator());
    data->path_initial_len = data->path.len();
    source.client->mini_buffer_completion_cache.engine_context.data = data;

    source.client->mini_buffer_completion_cache.engine_context.cleanup = [](void* _data) {
        Find_File_Completion_Engine_Data* data = (Find_File_Completion_Engine_Data*)_data;
        for (size_t i = data->directories.len(); i-- > 0;) {
            (void)data->directories[i].drop();
        }
        data->path.drop(cz::heap_allocator());
        data->ignore_rules.drop();
        cz::heap_allocator().dealloc(data);
    };
}

}
}
