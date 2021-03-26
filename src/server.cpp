#include "server.hpp"

#include <limits.h>
#include <stdio.h>
#include <Tracy.hpp>
#include <algorithm>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/mutex.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "custom/config.hpp"
#include "insert.hpp"
#include "movement.hpp"
#include "tracy_format.hpp"

namespace mag {

struct Run_Jobs_Data {
    cz::Mutex mutex;
    cz::Vector<Asynchronous_Job> jobs;
    cz::Vector<Synchronous_Job> pending_jobs;
    cz::String message;
    bool stop;

#ifdef TRACY_ENABLE
    tracy::SharedLockableCtx* mutex_context;
#endif
};

struct Run_Jobs {
    Run_Jobs_Data* data;

    void operator()() {
        tracy::SetThreadName("Job thread");
        ZoneScoped;

        Asynchronous_Job_Handler handler = {};
        handler.pending_jobs.reserve(cz::heap_allocator(), 2);
        CZ_DEFER({
            for (size_t i = 0; i < handler.pending_jobs.len(); ++i) {
                handler.pending_jobs[i].kill(handler.pending_jobs[i].data);
            }
            handler.pending_jobs.drop(cz::heap_allocator());
        });

        cz::String queue_message = {};
        CZ_DEFER(queue_message.drop(cz::heap_allocator()));

        bool remove = false;
        while (1) {
            size_t i = 0;

            Asynchronous_Job job;
            {
                ZoneScopedN("job thread find job");

#ifdef TRACY_ENABLE
                const auto run_after = data->mutex_context->BeforeLock();
#endif
                data->mutex.lock();
#ifdef TRACY_ENABLE
                if (run_after) {
                    data->mutex_context->AfterLock();
                }
#endif

#ifdef TRACY_ENABLE
                CZ_DEFER(data->mutex_context->AfterUnlock());
#endif
                CZ_DEFER(data->mutex.unlock());

                if (queue_message.len() != 0) {
                    std::swap(data->message, queue_message);
                    queue_message.set_len(0);
                }

                if (remove) {
                    data->jobs.remove(i);
                    if (i > 0) {
                        --i;
                    }
                    remove = false;
                }

                if (data->stop) {
                    return;
                }

                data->pending_jobs.reserve(cz::heap_allocator(), handler.pending_jobs.len());
                data->pending_jobs.append(handler.pending_jobs);
                handler.pending_jobs.set_len(0);

                if (data->jobs.len() == 0) {
                    i = 0;
                    goto sleep;
                }

                if (i == data->jobs.len()) {
                    i = 0;
                }

                job = data->jobs[i];
                ++i;
            }

            {
                ZoneScopedN("job thread run job");
                try {
                    if (job.tick(&handler, job.data)) {
                        remove = true;
                    }
                } catch (std::exception& ex) {
                    cz::Str prefix = "Job failed with message: ";
                    cz::Str message = ex.what();
                    queue_message.reserve(cz::heap_allocator(), prefix.len + message.len);
                    queue_message.append(prefix);
                    queue_message.append(message);
                    remove = true;
                }
            }

            if (false) {
            sleep:
                ZoneScopedN("job thread sleeping");
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        }
    }
};

void Server::init() {
    auto data = cz::heap_allocator().alloc<Run_Jobs_Data>();
    job_data_ = data;

    data->mutex.init();
    data->jobs = {};
    data->pending_jobs = {};
    data->message = {};
    data->stop = false;

#ifdef TRACY_ENABLE
    data->mutex_context = new tracy::SharedLockableCtx([]() -> const tracy::SourceLocationData* {
        static constexpr tracy::SourceLocationData srcloc{nullptr, "mag::Server", __FILE__,
                                                          __LINE__, 0};
        return &srcloc;
    }());
#endif

    job_thread = new std::thread(Run_Jobs{data});

    editor.create();
}

void Server::drop() {
    auto data = (Run_Jobs_Data*)job_data_;

    {
#ifdef TRACY_ENABLE
        const auto run_after = data->mutex_context->BeforeLock();
#endif
        data->mutex.lock();
#ifdef TRACY_ENABLE
        if (run_after) {
            data->mutex_context->AfterLock();
        }
#endif

#ifdef TRACY_ENABLE
        CZ_DEFER(data->mutex_context->AfterUnlock());
#endif
        CZ_DEFER(data->mutex.unlock());

        data->stop = true;
    }

    job_thread->join();
    delete job_thread;

    data->mutex.drop();

    for (size_t i = 0; i < data->jobs.len(); ++i) {
        data->jobs[i].kill(data->jobs[i].data);
    }
    data->jobs.drop(cz::heap_allocator());

    for (size_t i = 0; i < data->pending_jobs.len(); ++i) {
        data->pending_jobs[i].kill(data->pending_jobs[i].data);
    }
    data->pending_jobs.drop(cz::heap_allocator());

    data->message.drop(cz::heap_allocator());

#ifdef TRACY_ENABLE
    delete data->mutex_context;
#endif

    cz::heap_allocator().dealloc(data);

    pending_message.drop(cz::heap_allocator());

    editor.drop();
}

bool Server::slurp_jobs() {
    ZoneScoped;

    auto data = (Run_Jobs_Data*)job_data_;

#ifdef TRACY_ENABLE
    const auto run_after = data->mutex_context->BeforeLock();
#endif
    data->mutex.lock();
#ifdef TRACY_ENABLE
    if (run_after) {
        data->mutex_context->AfterLock();
    }
#endif

#ifdef TRACY_ENABLE
    CZ_DEFER(data->mutex_context->AfterUnlock());
#endif
    CZ_DEFER(data->mutex.unlock());

    data->jobs.reserve(cz::heap_allocator(), editor.pending_jobs.len());
    data->jobs.append(editor.pending_jobs);
    editor.pending_jobs.set_len(0);

    editor.synchronous_jobs.reserve(cz::heap_allocator(), data->pending_jobs.len());
    editor.synchronous_jobs.append(data->pending_jobs);
    data->pending_jobs.set_len(0);

    std::swap(pending_message, data->message);

    return data->jobs.len() > 0;
}

bool Server::run_synchronous_jobs(Client* client) {
    ZoneScoped;

    if (pending_message.len() > 0) {
        client->show_message(&editor, pending_message);
        pending_message.set_len(0);
    }

    for (size_t i = 0; i < editor.synchronous_jobs.len();) {
        if (editor.synchronous_jobs[i].tick(&editor, client, editor.synchronous_jobs[i].data)) {
            editor.synchronous_jobs.remove(i);
            continue;
        }
        ++i;
    }

    return editor.synchronous_jobs.len() > 0;
}

Client Server::make_client() {
    Client client = {};
    Buffer_Id selected_buffer_id = {editor.buffers.len() - 1};

    Buffer messages = {};
    messages.type = Buffer::TEMPORARY;
    messages.name = cz::Str("*client messages*").duplicate(cz::heap_allocator());
    messages.read_only = true;
    Buffer_Id messages_id = editor.create_buffer(messages)->id;

    Buffer mini_buffer = {};
    mini_buffer.type = Buffer::TEMPORARY;
    mini_buffer.name = cz::Str("*client mini buffer*").duplicate(cz::heap_allocator());
    Buffer_Id mini_buffer_id = editor.create_buffer(mini_buffer)->id;

    client.init(selected_buffer_id, mini_buffer_id, messages_id);

    return client;
}

static bool is_word_char(char c) {
    return cz::is_alnum(c) || c == '_';
}
static bool can_merge_insert(cz::Str str, char code) {
    char last = str[str.len - 1];
    return last == code || (is_word_char(last) && is_word_char(code));
}

static void command_insert_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    char code = source.keys[0].code;

    // Merge spaces into tabs.
    if (buffer->mode.use_tabs && code == ' ' && buffer->mode.tab_width > 0) {
        // See if there are any cursors we want to merge at.
        cz::Slice<Cursor> cursors = window->cursors;
        Contents_Iterator it = buffer->contents.start();
        size_t merge_tab = 0;
        for (size_t e = 0; e < cursors.len; ++e) {
            // No space before so we can't merge.
            if (cursors[e].point == 0) {
                continue;
            }

            it.advance_to(cursors[e].point);

            uint64_t column = get_visual_column(buffer->mode, it);

            // If the character before is not a space we can't merge.
            it.retreat();
            if (it.get() != ' ') {
                continue;
            }

            uint64_t end = it.position + 1;
            while (!it.at_bob() && end - it.position + 1 < buffer->mode.tab_width) {
                it.retreat();
                if (it.get() != ' ') {
                    it.advance();
                    break;
                }
            }

            // And if we're not going to hit a tab level we can't use a tab.
            if ((column + 1) % buffer->mode.tab_width == 0 &&
                end - it.position + 1 == buffer->mode.tab_width) {
                ++merge_tab;
            }
        }

        if (merge_tab > 0) {
            Transaction transaction;
            transaction.init(cursors.len + merge_tab, buffer->mode.tab_width);
            CZ_DEFER(transaction.drop());

            char* buf = (char*)transaction.value_allocator().alloc({buffer->mode.tab_width - 1, 1});
            memset(buf, ' ', buffer->mode.tab_width - 1);
            SSOStr spaces = SSOStr::from_constant({buf, buffer->mode.tab_width - 1});

            int64_t offset = 0;
            if (cursors.len > 1) {
                it.retreat_to(cursors[0].point);
            }
            for (size_t c = 0; c < cursors.len; ++c) {
                it.advance_to(cursors[c].point);

                if (it.position > 0) {
                    uint64_t column = get_visual_column(buffer->mode, it);

                    uint64_t end = it.position;
                    it.retreat();

                    // If the character before is not a space we can't merge.
                    // And if we're not going to hit a tab level we can't use a tab.
                    if (it.get() == ' ' && (column + 1) % buffer->mode.tab_width == 0) {
                        while (!it.at_bob()) {
                            it.retreat();
                            if (it.get() != ' ') {
                                it.advance();
                                break;
                            }
                        }

                        if (end - it.position + 1 == buffer->mode.tab_width) {
                            // Remove the spaces.
                            Edit remove;
                            remove.position = it.position + offset;
                            remove.value = spaces;
                            remove.flags = Edit::REMOVE;
                            transaction.push(remove);

                            // Insert the character.
                            Edit insert;
                            insert.position = it.position + offset;
                            insert.value = SSOStr::from_char('\t');
                            insert.flags = Edit::INSERT;
                            transaction.push(insert);

                            offset -= end - it.position;
                            ++offset;
                            continue;
                        }
                    }

                    // Reset if we arent replacing with a tab.
                    it.advance_to(end);
                }

                // Insert the character.
                Edit insert;
                insert.position = it.position + offset;
                insert.value = SSOStr::from_char(code);
                insert.flags = Edit::INSERT;
                transaction.push(insert);

                ++offset;
            }

            // Don't merge edits around tab replacement.
            transaction.commit(buffer, nullptr);
            return;
        }
    }

    if (buffer->check_last_committer(command_insert_char, window->cursors)) {
        CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len());
        Commit commit = buffer->commits[buffer->commit_index - 1];
        size_t len = commit.edits[0].value.len();
        if (len < SSOStr::MAX_SHORT_LEN && can_merge_insert(commit.edits[0].value.as_str(), code)) {
            CZ_DEBUG_ASSERT(commit.edits.len == window->cursors.len());
            buffer->undo();
            // We don't need to update cursors here because insertion doesn't care.

            Transaction transaction;
            transaction.init(commit.edits.len, 0);
            CZ_DEFER(transaction.drop());

            for (size_t e = 0; e < commit.edits.len; ++e) {
                CZ_DEBUG_ASSERT(commit.edits[e].value.is_short());
                CZ_DEBUG_ASSERT(commit.edits[e].value.len() == len);

                Edit edit;
                memcpy(edit.value.short_._buffer, commit.edits[e].value.short_._buffer, len);
                edit.value.short_._buffer[len] = code;
                edit.value.short_.set_len(len + 1);
                edit.position = commit.edits[e].position + e;
                edit.flags = Edit::INSERT;
                transaction.push(edit);
            }

            transaction.commit(buffer, command_insert_char);

            return;
        }
    }

    insert_char(buffer, window, code, command_insert_char);
}

#ifndef NDEBUG
struct File_Wrapper {
    FILE* file;

    File_Wrapper(const char* f, const char* m) { file = fopen(f, m); }

    ~File_Wrapper() {
        if (file) {
            fclose(file);
        }
    }
};
#endif

static void run_command(Command command, Editor* editor, Command_Source source) {
    try {
        ZoneScoped;
        TracyFormat(message, len, 1024, "run_command: %s", command.string);
        TracyMessage(message, len);
        command.function(editor, source);
    } catch (std::exception& ex) {
        source.client->show_message(editor, ex.what());
    }

#ifndef NDEBUG
    static File_Wrapper log("log.txt", "a");
    if (log.file) {
        fprintf(log.file, "%s\n", command.string);
    }
#endif
}

static bool lookup_key_press(cz::Slice<Key> key_chain,
                             size_t start,
                             Command* command,
                             size_t* end,
                             size_t* max_depth,
                             Key_Map* map) {
    // map can be null if the user hasn't configured a Mode
    // to have a special key map which is quite likely.
    if (map == nullptr) {
        return false;
    }

    *end = start;

    // Record the max depth so we know how many keys to delete.
    CZ_DEFER(*max_depth = std::max(*max_depth, *end - start));

    while (1) {
        // We need more keys to get to a command.
        if (*end == key_chain.len) {
            command->function = nullptr;
            return true;
        }

        // Look up this key in this level of the tree.
        Key_Bind* bind = map->lookup(key_chain[*end]);
        ++*end;

        // No key is bound in this key map.  So break.
        if (bind == nullptr) {
            return false;
        }

        // A command is bound so record the number of keys consumed and the command.
        if (bind->is_command) {
            *command = bind->v.command;
            CZ_DEBUG_ASSERT(command->function);
            return true;
        }

        // Descend one level.
        map = bind->v.map;
    }
}

static bool lookup_key_press_completion(cz::Slice<Key> key_chain,
                                        size_t start,
                                        Command* command,
                                        size_t* end,
                                        size_t* max_depth,
                                        Editor* editor,
                                        Client* client) {
    Window_Unified* window = client->selected_window();
    if (!window->completing) {
        return false;
    }

    WITH_CONST_WINDOW_BUFFER(window);
    if (!buffer->mode.completion_key_map) {
        return false;
    }

    return lookup_key_press(key_chain, start, command, end, max_depth,
                            buffer->mode.completion_key_map);
}

static bool lookup_key_press_buffer(cz::Slice<Key> key_chain,
                                    size_t start,
                                    Command* command,
                                    size_t* end,
                                    size_t* max_depth,
                                    Editor* editor,
                                    Client* client) {
    WITH_CONST_SELECTED_BUFFER(client);
    return lookup_key_press(key_chain, start, command, end, max_depth, buffer->mode.key_map);
}

static bool lookup_key_press_global(cz::Slice<Key> key_chain,
                                    size_t start,
                                    Command* command,
                                    size_t* end,
                                    size_t* max_depth,
                                    Editor* editor) {
    return lookup_key_press(key_chain, start, command, end, max_depth, &editor->key_map);
}

static bool handle_key_press_insert(cz::Slice<Key> key_chain,
                                    size_t start,
                                    Command* command,
                                    size_t* end,
                                    size_t* max_depth) {
    Key key = key_chain[start];
    if (key.modifiers == 0 && ((key.code <= UCHAR_MAX && cz::is_print(key.code)) ||
                               key.code == '\t' || key.code == '\n')) {
        *command = {command_insert_char, "command_insert_char"};
        *end = start = 1;
        return true;
    }
    return false;
}

void Server::receive(Client* client, Key key) {
    ZoneScoped;

    client->key_chain.reserve(cz::heap_allocator(), 1);
    client->key_chain.push(key);

    cz::Slice<Key> key_chain = client->key_chain;
    size_t start = 0;
    while (start < key_chain.len) {
        Command command;
        size_t end;
        size_t max_depth = 1;

        // Try the different key maps or fall back to trying
        // to convert the key press to inserting text.
        if (lookup_key_press_completion(key_chain, start, &command, &end, &max_depth, &editor,
                                        client) ||
            lookup_key_press_buffer(key_chain, start, &command, &end, &max_depth, &editor,
                                    client) ||
            lookup_key_press_global(key_chain, start, &command, &end, &max_depth, &editor) ||
            handle_key_press_insert(key_chain, start, &command, &end, &max_depth)) {
            // We need more keys before we can run a command.
            if (command.function == nullptr) {
                break;
            }

            // Make the source of the command.
            Command_Source source;
            source.client = client;
            source.keys = {key_chain.start() + start, end - start};
            source.previous_command = previous_command.function;

            // Update the state variables.
            previous_command = command;
            start = end;

            // Run the command.
            run_command(command, &editor, source);
        } else {
            // Print a message that this key press failed.
            cz::String message = {};
            CZ_DEFER(message.drop(cz::heap_allocator()));

            cz::Str prefix = "Unbound key chain:";
            message.reserve(cz::heap_allocator(),
                            prefix.len + stringify_key_max_size * max_depth + max_depth);
            message.append(prefix);

            CZ_DEBUG_ASSERT(max_depth > 0);
            for (size_t i = start; i < start + max_depth; ++i) {
                message.push(' ');
                stringify_key(&message, key_chain[i]);
            }

            client->show_message(&editor, message);
            previous_command = {};

            // Discard the number of keys consumed.
            start += max_depth;
        }
    }

    client->key_chain.remove_range(0, start);
}

}
