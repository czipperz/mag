#include "find_file.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "git.hpp"

namespace mag {
namespace git {

// todo: redo how completion works so we don't have this hack
static char* git_find_file_directory;

static void git_find_file_completion_engine(Editor*, Completion_Engine_Context* context) {
    const char* args[] = {"git", "ls-files", nullptr};
    cz::Process_Options options;
    options.working_directory = git_find_file_directory;
    run_command_for_completion_results(context, args, options);
}

static void command_git_find_file_response(Editor* editor,
                                           Client* client,
                                           cz::Str file,
                                           void* data) {
    cz::Str gffd = git_find_file_directory;

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    path.reserve(cz::heap_allocator(), gffd.len + 1 + file.len + 1);
    path.append(gffd);
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
        if (!get_git_top_level(source.client, buffer->directory.buffer(), cz::heap_allocator(),
                               &top_level_path)) {
            return;
        }
    }

    free(git_find_file_directory);
    git_find_file_directory = (char*)malloc(top_level_path.len() + 1);
    memcpy(git_find_file_directory, top_level_path.buffer(), top_level_path.len());
    git_find_file_directory[top_level_path.len()] = '\0';

    source.client->show_dialog(editor, "Git Find File: ", git_find_file_completion_engine,
                               command_git_find_file_response, nullptr);
}

}
}
