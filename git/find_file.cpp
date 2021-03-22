#include "find_file.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "git.hpp"

namespace mag {
namespace git {

static bool git_find_file_completion_engine(Editor*,
                                            Completion_Engine_Context* context,
                                            bool is_initial_frame) {
    cz::Str args[] = {"git", "ls-files"};
    cz::Process_Options options;
    options.working_directory = (char*)context->data;
    return run_command_for_completion_results(context, args, options, is_initial_frame);
}

static void command_git_find_file_response(Editor* editor,
                                           Client* client,
                                           cz::Str file,
                                           void* data) {
    cz::Str directory = (char*)data;

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    path.reserve(cz::heap_allocator(), directory.len + 1 + file.len + 1);
    path.append(directory);
    path.push('/');
    path.append(file);
    path.null_terminate();

    open_file(editor, client, path);
}

void command_git_find_file(Editor* editor, Command_Source source) {
    cz::String top_level_path = {};
    CZ_DEFER(top_level_path.drop(cz::heap_allocator()));
    {
        WITH_SELECTED_BUFFER(source.client);
        if (!get_git_top_level(editor, source.client, buffer->directory.buffer(),
                               cz::heap_allocator(), &top_level_path)) {
            return;
        }
    }

    char* directory = top_level_path.clone_null_terminate(cz::heap_allocator()).buffer();

    source.client->show_dialog(editor, "Git Find File: ", git_find_file_completion_engine,
                               command_git_find_file_response, directory);
    source.client->mini_buffer_completion_cache.engine_context.data = directory;
    // Don't deallocate the directory in the completion engine context because
    // we deallocate when the dialog is closed by passing it to show_dialog.
    source.client->mini_buffer_completion_cache.engine_context.cleanup = [](void*) {};
}

}
}
