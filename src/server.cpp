#include "server.hpp"

#include <limits.h>
#include <stdio.h>
#include <Tracy.hpp>
#include <algorithm>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/mutex.hpp>
#include <cz/semaphore.hpp>
#include "basic/commands.hpp"
#include "client.hpp"
#include "command_macros.hpp"
#include "custom/config.hpp"
#include "insert.hpp"
#include "movement.hpp"
#include "tracy_format.hpp"

namespace mag {

struct Async_Context {
    cz::Mutex mutex;
    bool permitted;
    Server* server;
    Client* client;
};

struct Run_Jobs_Data {
    cz::Semaphore added_asynchronous_job_signal;
    cz::Mutex mutex;
    cz::Vector<Asynchronous_Job> jobs;
    cz::Vector<Synchronous_Job> pending_jobs;
    cz::String message;
    bool stop;

    Async_Context async_context;
};

bool Asynchronous_Job_Handler::try_sync_lock(Server** server, Client** client) {
    Async_Context* ctx = (Async_Context*)async_context;

    if (!ctx->mutex.try_lock()) {
        return false;
    }

    if (!ctx->permitted) {
        ctx->mutex.unlock();
        return false;
    }

    *server = ctx->server;
    *client = ctx->client;
    return true;
}

void Asynchronous_Job_Handler::sync_unlock() {
    Async_Context* ctx = (Async_Context*)async_context;
    ctx->mutex.unlock();
}

struct Run_Jobs {
    Run_Jobs_Data* data;

    void operator()() {
        tracy::SetThreadName("Job thread");
        ZoneScoped;

        Asynchronous_Job_Handler handler = {};
        handler.async_context = &data->async_context;

        handler.pending_synchronous_jobs.reserve(cz::heap_allocator(), 2);
        CZ_DEFER({
            for (size_t i = 0; i < handler.pending_synchronous_jobs.len(); ++i) {
                handler.pending_synchronous_jobs[i].kill(handler.pending_synchronous_jobs[i].data);
            }
            handler.pending_synchronous_jobs.drop(cz::heap_allocator());
        });

        handler.pending_asynchronous_jobs.reserve(cz::heap_allocator(), 2);
        CZ_DEFER({
            for (size_t i = 0; i < handler.pending_asynchronous_jobs.len(); ++i) {
                handler.pending_asynchronous_jobs[i].kill(
                    handler.pending_asynchronous_jobs[i].data);
            }
            handler.pending_asynchronous_jobs.drop(cz::heap_allocator());
        });

        cz::String queue_message = {};
        CZ_DEFER(queue_message.drop(cz::heap_allocator()));

        size_t job_index = 0;
        bool started = false;

        bool remove = false;
        bool made_progress = false;
        while (1) {
            Asynchronous_Job job;
            {
                ZoneScopedN("job thread find job");

                data->mutex.lock();
                CZ_DEFER(data->mutex.unlock());

                if (queue_message.len() != 0) {
                    std::swap(data->message, queue_message);
                    queue_message.set_len(0);
                }

                if (remove) {
                    data->jobs.remove(job_index);
                    remove = false;
                }

                if (data->stop) {
                    if (started) {
                        FrameMarkEnd("job thread");
                    }
                    return;
                }

                // Send synchronous jobs to the Editor.
                data->pending_jobs.reserve(cz::heap_allocator(),
                                           handler.pending_synchronous_jobs.len());
                data->pending_jobs.append(handler.pending_synchronous_jobs);
                handler.pending_synchronous_jobs.set_len(0);

                // Add asynchronous jobs to the list.
                data->jobs.reserve(cz::heap_allocator(), handler.pending_asynchronous_jobs.len());
                data->jobs.append(handler.pending_asynchronous_jobs);
                handler.pending_asynchronous_jobs.set_len(0);

                if (data->jobs.len() == 0) {
                    job_index = 0;
                    made_progress = false;
                    goto wait_for_more_jobs;
                }

                if (job_index == data->jobs.len()) {
                    job_index = 0;
                    if (!made_progress) {
                        goto sleep;
                    }
                    made_progress = false;
                }

                job = data->jobs[job_index];
            }

            if (!started) {
                FrameMarkStart("job thread");
                started = true;
            }

            {
                ZoneScopedN("job thread run job");
                try {
                    Job_Tick_Result result = job.tick(&handler, job.data);
                    if (result == Job_Tick_Result::FINISHED) {
                        remove = true;
                    } else if (result == Job_Tick_Result::MADE_PROGRESS) {
                        made_progress = true;
                    }
                } catch (std::exception& ex) {
                    cz::Str prefix = "Job failed with message: ";
                    cz::Str message = ex.what();
                    queue_message.reserve(cz::heap_allocator(), prefix.len + message.len);
                    queue_message.append(prefix);
                    queue_message.append(message);
                    remove = true;
                }

                // Go to the next job.  If we remove then the next job will
                // be shifted into our position so there's nothing to do.
                if (!remove) {
                    ++job_index;
                }
            }

            if (false) {
            wait_for_more_jobs:
                if (started) {
                    FrameMarkEnd("job thread");
                    started = false;
                }

                ZoneScopedN("job thread waiting for more jobs");
                data->added_asynchronous_job_signal.acquire();
            }

            if (false) {
            sleep:
                ZoneScopedN("job thread sleep");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
};

void Server::setup_async_context(Client* client) {
    auto data = (Run_Jobs_Data*)job_data_;
    data->async_context.mutex.lock();
    CZ_DEFER(data->async_context.mutex.unlock());

    data->async_context.server = this;
    data->async_context.client = client;
}

void Server::set_async_locked(bool locked) {
    auto data = (Run_Jobs_Data*)job_data_;
    data->async_context.mutex.lock();
    CZ_DEFER(data->async_context.mutex.unlock());

    data->async_context.permitted = !locked;
}

void Server::init() {
    auto data = cz::heap_allocator().alloc<Run_Jobs_Data>();
    job_data_ = data;

    *data = {};
    data->added_asynchronous_job_signal.init(0);
    data->mutex.init();

    data->async_context.mutex.init();
    data->async_context.permitted = false;

    job_thread = new std::thread(Run_Jobs{data});

    editor.create();
}

void Server::drop() {
    auto data = (Run_Jobs_Data*)job_data_;

    {
        data->mutex.lock();
        CZ_DEFER(data->mutex.unlock());

        data->stop = true;
    }

    // If the other thread is stalled on the semaphore then release it.
    data->added_asynchronous_job_signal.release();

    job_thread->join();
    delete job_thread;

    data->added_asynchronous_job_signal.drop();
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

    data->async_context.mutex.drop();

    cz::heap_allocator().dealloc(data);

    pending_message.drop(cz::heap_allocator());

    editor.drop();
}

bool Server::slurp_jobs() {
    ZoneScoped;

    auto data = (Run_Jobs_Data*)job_data_;

    data->mutex.lock();
    CZ_DEFER(data->mutex.unlock());

    data->jobs.reserve(cz::heap_allocator(), editor.pending_jobs.len());
    data->jobs.append(editor.pending_jobs);
    if (editor.pending_jobs.len() > 0) {
        data->added_asynchronous_job_signal.release();
    }
    editor.pending_jobs.set_len(0);

    editor.synchronous_jobs.reserve(cz::heap_allocator(), data->pending_jobs.len());
    editor.synchronous_jobs.append(data->pending_jobs);
    data->pending_jobs.set_len(0);

    std::swap(pending_message, data->message);

    return data->jobs.len() > 0;
}

bool Server::run_synchronous_jobs(Client* client) {
    ZoneScoped;

    bool ran_any_jobs = false;

    if (pending_message.len() > 0) {
        client->show_message(&editor, pending_message);
        pending_message.set_len(0);
    }

    for (size_t i = 0; i < editor.synchronous_jobs.len();) {
        ran_any_jobs = true;
        Synchronous_Job job = editor.synchronous_jobs[i];
        Job_Tick_Result result = job.tick(&editor, client, job.data);
        if (result == Job_Tick_Result::FINISHED) {
            editor.synchronous_jobs.remove(i);
            continue;
        }
        ++i;
    }

    if (client->_message.interactive_response_callback) {
        ran_any_jobs = true;

        cz::String mini_buffer_contents = {};
        CZ_DEFER(mini_buffer_contents.drop(cz::heap_allocator()));
        {
            WITH_CONST_WINDOW_BUFFER(client->mini_buffer_window());
            mini_buffer_contents = buffer->contents.stringify(cz::heap_allocator());
        }

        client->_message.interactive_response_callback(&editor, client, mini_buffer_contents,
                                                       client->_message.response_callback_data);
    }

    return ran_any_jobs;
}

Client Server::make_client() {
    Client client = {};

    cz::Arc<Buffer_Handle> selected_buffer_handle = editor.buffers.last();

    Buffer messages = {};
    messages.type = Buffer::TEMPORARY;
    messages.name = cz::Str("*client messages*").clone(cz::heap_allocator());
    messages.read_only = true;
    cz::Arc<Buffer_Handle> messages_handle = editor.create_buffer(messages);

    Buffer mini_buffer = {};
    mini_buffer.type = Buffer::TEMPORARY;
    mini_buffer.name = cz::Str("*client mini buffer*").clone(cz::heap_allocator());
    cz::Arc<Buffer_Handle> mini_buffer_handle = editor.create_buffer(mini_buffer);

    client.init(selected_buffer_handle, mini_buffer_handle, messages_handle);

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

    CZ_ASSERT(source.keys[0].code <= UCHAR_MAX);

    char code = source.keys[0].code;

    // If temporarily showing marks then first delete the region then
    // type.  This makes 2 edits so you can redo inbetween these edits.
    if (window->show_marks == 2) {
        delete_regions(buffer, window);
        insert_char(buffer, window, code, command_insert_char);
        return;
    }

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
            transaction.init(buffer);
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
            transaction.commit();
            return;
        }
    }

    if (source.previous_command.function == command_insert_char &&
        buffer->check_last_committer(command_insert_char, window->cursors)) {
        CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len());
        Commit commit = buffer->commits[buffer->commit_index - 1];
        size_t len = commit.edits[0].value.len();
        if (len < SSOStr::MAX_SHORT_LEN && can_merge_insert(commit.edits[0].value.as_str(), code)) {
            CZ_DEBUG_ASSERT(commit.edits.len == window->cursors.len());
            buffer->undo();
            // We don't need to update cursors here because insertion doesn't care.

            Transaction transaction;
            transaction.init(buffer);
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

            transaction.commit(command_insert_char);

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

#ifdef TRACY_ENABLE
        {
            cz::Str prefix = "run_command: ";
            cz::Str command_string = command.string;

            cz::String message = {};
            CZ_DEFER(message.drop(cz::heap_allocator()));
            message.reserve(cz::heap_allocator(),
                            prefix.len + command_string.len + 3 - 1 +
                                (1 + stringify_key_max_size) * source.keys.len);
            message.append(prefix);
            message.append(command_string);
            message.append(" (");
            for (size_t i = 0; i < source.keys.len; ++i) {
                if (i > 0) {
                    message.push(' ');
                }
                stringify_key(&message, source.keys[i]);
            }
            message.append(")");

            TracyMessage(message.buffer(), message.len());
        }
#endif

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

static bool lookup_key_press_inner(cz::Slice<Key> key_chain,
                                   size_t start,
                                   Command* command,
                                   size_t* end,
                                   size_t* max_depth,
                                   const Key_Map* map) {
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
        const Key_Bind* bind = map->lookup(key_chain[*end]);
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

static bool recursively_remap_and_lookup_key_press(const Key_Remap& remap,
                                                   size_t index,
                                                   cz::Vector<Key>& key_chain,
                                                   size_t start,
                                                   Command* command,
                                                   size_t* end,
                                                   size_t* max_depth,
                                                   const Key_Map* map) {
    // Advance through keys that don't have alternatives.
    while (index < key_chain.len() && !remap.bound(key_chain[index])) {
        ++index;
    }

    // No alternatives so look up at this point.
    if (index == key_chain.len()) {
        return lookup_key_press_inner(key_chain, start, command, end, max_depth, map);
    }

    // Lookup the input chain.
    if (recursively_remap_and_lookup_key_press(remap, index + 1, key_chain, start, command, end,
                                               max_depth, map)) {
        return true;
    }

    // Apply the mapping at this key.
    Key diff = remap.get(key_chain[index]);
    Key old = key_chain[index];
    key_chain[index] = diff;
    CZ_DEFER(key_chain[index] = old);

    // And recurse with the alternate key chain.
    return recursively_remap_and_lookup_key_press(remap, index + 1, key_chain, start, command, end,
                                                  max_depth, map);
}

static bool lookup_key_press(cz::Slice<Key> key_chain_orig,
                             size_t start,
                             Command* command,
                             size_t* end,
                             size_t* max_depth,
                             const Key_Remap& remap,
                             const Key_Map* map) {
    // `map` can be null if the user hasn't configured a `Mode` to have a special key map.
    if (map == nullptr) {
        return false;
    }

    // We want to generate 2^n combinations for each remapped key.
    // We'll duplicate the input to a temporary so we can play with it.
    cz::Vector<Key> key_chain = {};
    CZ_DEFER(key_chain.drop(cz::heap_allocator()));
    key_chain.reserve(cz::heap_allocator(), key_chain_orig.len);
    key_chain.append(key_chain_orig);

    // Transfer to the recursive combinator.
    return recursively_remap_and_lookup_key_press(remap, 0, key_chain, start, command, end,
                                                  max_depth, map);
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
    return lookup_key_press(key_chain, start, command, end, max_depth, editor->key_remap,
                            &buffer->mode.completion_key_map);
}

static bool lookup_key_press_buffer(cz::Slice<Key> key_chain,
                                    size_t start,
                                    Command* command,
                                    size_t* end,
                                    size_t* max_depth,
                                    Editor* editor,
                                    Client* client) {
    WITH_CONST_SELECTED_BUFFER(client);
    return lookup_key_press(key_chain, start, command, end, max_depth, editor->key_remap,
                            &buffer->mode.key_map);
}

static bool lookup_key_press_global(cz::Slice<Key> key_chain,
                                    size_t start,
                                    Command* command,
                                    size_t* end,
                                    size_t* max_depth,
                                    Editor* editor) {
    return lookup_key_press(key_chain, start, command, end, max_depth, editor->key_remap,
                            &editor->key_map);
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
        *end = start + 1;
        return true;
    }
    return false;
}

void Server::receive(Client* client, Key key) {
    ZoneScoped;

    client->key_chain.reserve(cz::heap_allocator(), 1);
    client->key_chain.push(key);

    while (client->key_chain_offset < client->key_chain.len()) {
        Command command;
        size_t end;
        size_t max_depth = 1;

        // Try the different key maps or fall back to trying
        // to convert the key press to inserting text.
        if (lookup_key_press_completion(client->key_chain, client->key_chain_offset, &command, &end,
                                        &max_depth, &editor, client) ||
            lookup_key_press_buffer(client->key_chain, client->key_chain_offset, &command, &end,
                                    &max_depth, &editor, client) ||
            lookup_key_press_global(client->key_chain, client->key_chain_offset, &command, &end,
                                    &max_depth, &editor) ||
            handle_key_press_insert(client->key_chain, client->key_chain_offset, &command, &end,
                                    &max_depth)) {
            // We need more keys before we can run a command.
            if (command.function == nullptr) {
                break;
            }
        } else {
            command = {basic::command_invalid, "command_invalid"};
        }

        // Make the source of the command.
        Command_Source source;
        source.client = client;
        source.previous_command = previous_command;

        cz::Vector<Key> temp = {};
        CZ_DEFER(temp.drop(cz::heap_allocator()));

        // Update the state variables.
        previous_command = command;
        if (client->record_key_presses) {
            source.keys = {client->key_chain.start() + client->key_chain_offset,
                           end - client->key_chain_offset};
            client->key_chain_offset = end;
        } else {
            temp.reserve(cz::heap_allocator(), end);
            temp.append({client->key_chain.start(), end});
            source.keys = temp;
            client->key_chain.remove_range(0, end);
            client->key_chain_offset = 0;
        }

        // Run the command.
        run_command(command, &editor, source);
    }
}

}
