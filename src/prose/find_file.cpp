#include "find_file.hpp"

#include <cz/arc.hpp>
#include <cz/defer.hpp>
#include <cz/directory.hpp>
#include <cz/file.hpp>
#include <cz/heap.hpp>
#include <cz/mutex.hpp>
#include <cz/path.hpp>
#include <cz/process.hpp>
#include <cz/sort.hpp>
#include <cz/util.hpp>
#include "core/command_macros.hpp"
#include "core/file.hpp"
#include "prose/helpers.hpp"
#include "syntax/tokenize_path.hpp"
#include "version_control/ignore.hpp"

namespace mag {
namespace prose {

namespace find_file_ {
struct Find_File_Shared_Data {
    cz::Mutex mutex;

    cz::Vector<cz::Vector<cz::Str> > results;

    bool finished;

    cz::Buffer_Array results_buffer_array;

    void drop() {
        mutex.drop();

        for (size_t i = 0; i < results.len; ++i) {
            results[i].drop(cz::heap_allocator());
        }
        results.drop(cz::heap_allocator());

        results_buffer_array.drop();
    }
};

struct Directory {
    cz::Vector<cz::Str> entries;
    cz::Buffer_Array::Save_Point save_point;
};

struct Find_File_Job_Data {
    size_t path_initial_len;
    cz::String path;

    cz::Buffer_Array entries_buffer_array;
    cz::Vector<Directory> directories;

    cz::Buffer_Array results_buffer_array;

    bool already_started;
    cz::Vector<size_t> ignore_rules_offsets;
    cz::Vector<version_control::Ignore_Rules> ignore_rules_rules;

    cz::Arc_Weak<Find_File_Shared_Data> shared;

    void drop() {
        Find_File_Job_Data* data = this;

        data->results_buffer_array.drop();

        for (size_t i = 0; i < ignore_rules_rules.len; ++i) {
            data->ignore_rules_rules[i].drop();
        }
        data->ignore_rules_offsets.drop(cz::heap_allocator());
        data->ignore_rules_rules.drop(cz::heap_allocator());

        for (size_t i = data->directories.len; i-- > 0;) {
            (void)data->directories[i].entries.drop(cz::heap_allocator());
        }
        data->directories.drop(cz::heap_allocator());

        data->entries_buffer_array.drop();
        data->path.drop(cz::heap_allocator());
    }
};

struct Find_File_Completion_Engine_Data {
    cz::Arc<Find_File_Shared_Data> shared;
    cz::String path;
    bool finished;

    void drop() {
        if (shared.is_not_null()) {
            shared.drop();
        }
        path.drop(cz::heap_allocator());
    }
};
}
using namespace find_file_;

static bool load_directory(Find_File_Job_Data* data) {
    cz::Allocator entry_allocator = data->entries_buffer_array.allocator();

    // Get all files in the directory.
    Directory directory;
    directory.save_point = data->entries_buffer_array.save();
    directory.entries = {};

    cz::files(cz::heap_allocator(), entry_allocator, data->path.buffer, &directory.entries);

    // Then sort it into reverse order because we pop them from the end first.
    cz::sort(directory.entries, [](cz::Str* left, cz::Str* right) { return *left > *right; });
    data->directories.push(directory);

    // Load ignore rules.
    version_control::Ignore_Rules rules = {};
    version_control::find_ignore_rules(data->path.buffer, &rules);
    data->ignore_rules_offsets.reserve(cz::heap_allocator(), 1);
    data->ignore_rules_rules.reserve(cz::heap_allocator(), 1);
    data->ignore_rules_offsets.push(data->path.len - data->path_initial_len);
    data->ignore_rules_rules.push(rules);

    data->path.push('/');

    return true;
}

static Job_Tick_Result find_file_job_tick(Asynchronous_Job_Handler* handler, void* _data) {
    Find_File_Job_Data* data = (Find_File_Job_Data*)_data;

    // Try to acquire the shared data but don't lock it until later.  If it has
    // been deleted then our results are no longer needed so we can delete them.
    cz::Arc<Find_File_Shared_Data> shared;
    if (!data->shared.upgrade(&shared)) {
        data->drop();
        cz::heap_allocator().dealloc(data);
        return Job_Tick_Result::FINISHED;
    }
    CZ_DEFER(shared.drop());

    // First iteration setup.
    if (!data->already_started) {
        data->already_started = true;

        data->directories.reserve(cz::heap_allocator(), 32);
        data->entries_buffer_array.init();

        data->results_buffer_array.init();

        // Load first directory.
        if (!load_directory(data)) {
            {
                shared->mutex.lock();
                CZ_DEFER(shared->mutex.unlock());

                shared->finished = true;
            }

            data->drop();
            cz::heap_allocator().dealloc(data);
            return Job_Tick_Result::FINISHED;
        }
    }

    cz::Vector<cz::Str> results = {};
    results.reserve(cz::heap_allocator(), 128);

    while (results.remaining() > 0) {
    next_file:
        // Pop empty entries.
        while (data->directories.len > 0 && data->directories.last().entries.len == 0) {
            // Pop last name and trailing `/`.  Ex. `/home/abc/` -> `/home/`.
            data->path.pop();
            cz::path::pop_name(&data->path);

            // Delete the ignore rules for the directory.
            data->ignore_rules_offsets.pop();
            data->ignore_rules_rules.last().drop();
            data->ignore_rules_rules.pop();

            // Cleanup the directory.
            Directory directory = data->directories.pop();
            data->entries_buffer_array.restore(directory.save_point);
            directory.entries.drop(cz::heap_allocator());
        }

        // Test if we're completely done.
        if (data->directories.len == 0) {
            break;
        }

        const size_t old_len = data->path.len;

        // Get the absolute path to this entry.
        cz::Str entry = data->directories.last().entries.pop();
        data->path.reserve(cz::heap_allocator(), entry.len + 2);
        data->path.append(entry);
        data->path.null_terminate();

        // Skip ignored files.
        for (size_t i = 0; i < data->ignore_rules_rules.len; ++i) {
            cz::Str relpath =
                data->path.slice_start(data->path_initial_len + data->ignore_rules_offsets[i]);
            if (version_control::file_matches(data->ignore_rules_rules[i], relpath)) {
                data->path.len = old_len;
                goto next_file;
            }
        }

        // Non-directories are listed literally.  Don't follow symlinks so count them as files.
        if (!cz::file::is_directory_and_not_symlink(data->path.buffer) ||
            data->directories.remaining() == 0) {
            cz::Str result = data->path.slice_start(data->path_initial_len + 1);
            results.push(result.clone(data->results_buffer_array.allocator()));
            data->path.len = old_len;
            continue;
        }

        // Reached a directory entry so load it.
        // If loading fails we just ignore it and go to the next directory.
        (void)load_directory(data);
    }

    shared->mutex.lock();
    CZ_DEFER(shared->mutex.unlock());

    Job_Tick_Result result = Job_Tick_Result::MADE_PROGRESS;

    // If we stop early then we are done.
    if (results.remaining() > 0) {
        shared->finished = true;
        cz::swap(shared->results_buffer_array, data->results_buffer_array);
        data->drop();
        cz::heap_allocator().dealloc(data);
        result = Job_Tick_Result::FINISHED;
    }

    // If there are results then push them.
    if (results.len > 0) {
        shared->results.reserve(cz::heap_allocator(), 1);
        shared->results.push(results);
    } else {
        results.drop(cz::heap_allocator());
    }

    return result;
}

static void find_file_job_kill(void* _data) {
    Find_File_Job_Data* data = (Find_File_Job_Data*)_data;

    // If the shared state is still alive then mark ourselves as done.
    cz::Arc<Find_File_Shared_Data> shared;
    if (data->shared.upgrade(&shared)) {
        CZ_DEFER(shared.drop());

        shared->mutex.lock();
        CZ_DEFER(shared->mutex.unlock());

        shared->finished = true;
    }

    data->drop();
    cz::heap_allocator().dealloc(data);
}

static bool find_file_completion_engine(Editor* editor,
                                        Completion_Engine_Context* context,
                                        bool is_initial_frame) {
    Find_File_Completion_Engine_Data* data = (Find_File_Completion_Engine_Data*)context->data;

    if (is_initial_frame) {
        if (data->shared.is_not_null()) {
            data->shared.drop();
        }

        data->shared.init_copy({});
        data->shared->mutex.init();
        data->shared->results_buffer_array.init();

        Find_File_Job_Data* job_data = cz::heap_allocator().alloc<Find_File_Job_Data>();
        CZ_ASSERT(job_data);
        *job_data = {};
        job_data->path_initial_len = data->path.len;
        job_data->path = data->path.clone_null_terminate(cz::heap_allocator());
        job_data->shared = data->shared.clone_downgrade();
        job_data->entries_buffer_array.init();

        Asynchronous_Job job;
        job.tick = find_file_job_tick;
        job.kill = find_file_job_kill;
        job.data = job_data;
        editor->add_asynchronous_job(job);

        context->results_buffer_array.clear();
        context->results.len = 0;
    }

    if (data->finished) {
        return false;
    }

    // Take a local copy so we don't deallocate before we unlock.
    cz::Arc<Find_File_Shared_Data> shared = data->shared.clone();
    CZ_DEFER(shared.drop());

    shared->mutex.lock();
    CZ_DEFER(shared->mutex.unlock());

    bool changes = false;

    cz::Slice<cz::Vector<cz::Str> > results = shared->results;
    if (results.len > 0) {
        changes = true;

        size_t total = 0;
        for (size_t i = 0; i < results.len; ++i) {
            total += results[i].len;
        }

        context->results.reserve(total);

        for (size_t i = 0; i < results.len; ++i) {
            context->results.append(results[i]);
            results[i].drop(cz::heap_allocator());
        }

        shared->results.len = 0;
    }

    if (shared->finished) {
        data->finished = true;
    }

    return changes;
}

static void command_find_file_response(Editor* editor, Client* client, cz::Str file, void* data) {
    cz::Str directory = (char*)data;

    {
        WITH_CONST_SELECTED_BUFFER(client);
        push_jump(window, client, buffer);
    }

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    path.reserve(cz::heap_allocator(), directory.len + file.len + 1);
    path.append(directory);
    path.append(file);
    path.null_terminate();

    open_file_arg(editor, client, path);
}

template <class Copy_Directory>
static void find_file(Editor* editor,
                      Client* client,
                      const char* prompt,
                      Copy_Directory&& copy_directory) {
    cz::String directory = {};
    {
        WITH_CONST_SELECTED_NORMAL_BUFFER(client);
        if (!copy_directory(client, buffer, &directory)) {
            return;
        }
    }

    Dialog dialog = {};
    dialog.prompt = prompt;
    dialog.completion_engine = find_file_completion_engine;
    dialog.response_callback = command_find_file_response;
    dialog.response_callback_data = directory.buffer;
    dialog.next_token = syntax::path_next_token;
    client->show_dialog(dialog);

    auto data = cz::heap_allocator().alloc<Find_File_Completion_Engine_Data>();
    CZ_ASSERT(data);
    *data = {};
    data->path = directory.clone_null_terminate(cz::heap_allocator());
    client->mini_buffer_completion_cache.engine_context.data = data;

    client->mini_buffer_completion_cache.engine_context.cleanup = [](void* _data) {
        Find_File_Completion_Engine_Data* data = (Find_File_Completion_Engine_Data*)_data;
        data->drop();
        cz::heap_allocator().dealloc(data);
    };
}

REGISTER_COMMAND(command_find_file_in_current_directory);
void command_find_file_in_current_directory(Editor* editor, Command_Source source) {
    find_file(editor, source.client, "Find file in current directory: ", copy_buffer_directory);
}

REGISTER_COMMAND(command_find_file_in_version_control);
void command_find_file_in_version_control(Editor* editor, Command_Source source) {
    find_file(editor, source.client,
              "Find file in version control: ", copy_version_control_directory);
}

struct Diff_Master_Completion_Engine_Data {
    char* working_directory;
    Run_Command_For_Completion_Results runner;
};

static bool diff_master_completion_engine(Editor* editor,
                                          Completion_Engine_Context* context,
                                          bool is_initial_frame) {
    cz::Str args[] = {"bash", "-c", "git diff --name-only $(git merge-base origin/master HEAD)"};
    Diff_Master_Completion_Engine_Data* data = (Diff_Master_Completion_Engine_Data*)context->data;
    cz::Process_Options options;
    options.working_directory = data->working_directory;
#ifdef _WIN32
    options.hide_window = true;
#endif
    return data->runner.iterate(context, args, options, is_initial_frame);
}

REGISTER_COMMAND(command_find_file_diff_master);
void command_find_file_diff_master(Editor* editor, Command_Source source) {
    cz::String directory = {};
    {
        WITH_CONST_SELECTED_NORMAL_BUFFER(source.client);
        if (!copy_version_control_directory(source.client, buffer, &directory)) {
            return;
        }
    }

    Dialog dialog = {};
    dialog.prompt = "Find file diff master: ";
    dialog.completion_engine = diff_master_completion_engine;
    dialog.response_callback = command_find_file_response;
    dialog.response_callback_data = directory.buffer;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(dialog);

    auto data = cz::heap_allocator().alloc<Diff_Master_Completion_Engine_Data>();
    CZ_ASSERT(data);
    *data = {};
    data->working_directory = directory.clone_null_terminate(cz::heap_allocator()).buffer;
    source.client->mini_buffer_completion_cache.engine_context.data = data;

    source.client->mini_buffer_completion_cache.engine_context.cleanup = [](void* _data) {
        Diff_Master_Completion_Engine_Data* data = (Diff_Master_Completion_Engine_Data*)_data;
        cz::heap_allocator().dealloc({data->working_directory, 0});
        data->runner.drop();
        cz::heap_allocator().dealloc(data);
    };
}

}
}
