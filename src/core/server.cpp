#include "server.hpp"

#include <limits.h>
#include <stdio.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/mutex.hpp>
#include <cz/semaphore.hpp>
#include <cz/util.hpp>
#include <tracy/Tracy.hpp>
#include "basic/commands.hpp"
#include "core/client.hpp"
#include "core/command_macros.hpp"
#include "core/insert.hpp"
#include "core/movement.hpp"
#include "core/tracy_format.hpp"
#include "custom/config.hpp"

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
    std::atomic_size_t* num_uncompleted_async_jobs;
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
        tracy::SetThreadName("Mag job thread");

        Asynchronous_Job_Handler handler = {};
        handler.async_context = &data->async_context;

        handler.pending_synchronous_jobs.reserve(cz::heap_allocator(), 2);
        CZ_DEFER({
            for (size_t i = 0; i < handler.pending_synchronous_jobs.len; ++i) {
                handler.pending_synchronous_jobs[i].kill(handler.pending_synchronous_jobs[i].data);
            }
            handler.pending_synchronous_jobs.drop(cz::heap_allocator());
        });

        handler.pending_asynchronous_jobs.reserve(cz::heap_allocator(), 2);
        CZ_DEFER({
            for (size_t i = 0; i < handler.pending_asynchronous_jobs.len; ++i) {
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

                if (queue_message.len != 0) {
                    cz::swap(data->message, queue_message);
                    queue_message.len = 0;
                }

                if (remove) {
                    --*data->num_uncompleted_async_jobs;
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
                                           handler.pending_synchronous_jobs.len);
                data->pending_jobs.append(handler.pending_synchronous_jobs);
                handler.pending_synchronous_jobs.len = 0;

                // Add asynchronous jobs to the list.
                data->jobs.reserve(cz::heap_allocator(), handler.pending_asynchronous_jobs.len);
                data->jobs.append(handler.pending_asynchronous_jobs);
                handler.pending_asynchronous_jobs.len = 0;

                if (data->jobs.len == 0) {
                    job_index = 0;
                    made_progress = false;
                    goto wait_for_more_jobs;
                }

                if (job_index == data->jobs.len) {
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

    data->num_uncompleted_async_jobs = &editor.num_uncompleted_async_jobs;

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

    for (size_t i = 0; i < data->jobs.len; ++i) {
        data->jobs[i].kill(data->jobs[i].data);
    }
    data->jobs.drop(cz::heap_allocator());

    for (size_t i = 0; i < data->pending_jobs.len; ++i) {
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

    data->jobs.reserve(cz::heap_allocator(), editor.pending_jobs.len);
    data->jobs.append(editor.pending_jobs);
    if (editor.pending_jobs.len > 0) {
        editor.num_uncompleted_async_jobs += editor.pending_jobs.len;
        data->added_asynchronous_job_signal.release();
    }
    editor.pending_jobs.len = 0;

    editor.synchronous_jobs.reserve(cz::heap_allocator(), data->pending_jobs.len);
    editor.synchronous_jobs.append(data->pending_jobs);
    data->pending_jobs.len = 0;

    cz::swap(pending_message, data->message);

    return data->jobs.len > 0;
}

bool Server::send_pending_asynchronous_jobs() {
    if (editor.pending_jobs.len == 0) {
        return false;
    }

    return slurp_jobs();
}

bool Server::run_synchronous_jobs(Client* client, size_t start_index) {
    ZoneScoped;

    bool ran_any_jobs = false;

    if (pending_message.len > 0) {
        client->show_message(pending_message);
        pending_message.len = 0;
    }

    for (size_t i = start_index; i < editor.synchronous_jobs.len;) {
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
            WITH_CONST_WINDOW_BUFFER(client->mini_buffer_window(), client);
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

#ifdef CZ_DEBUG_ASSERTIONS
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

            TracyMessage(message.buffer, message.len);
            ZoneText(message.buffer + prefix.len, message.len - prefix.len);
        }
#endif

        command.function(editor, source);
    } catch (std::exception& ex) {
        source.client->show_message(ex.what());
    }

#ifdef CZ_DEBUG_ASSERTIONS
    static File_Wrapper log("mag-key.log", "a");
    if (log.file) {
        fprintf(log.file, "%s\n", command.string);
    }
#endif
}

static bool lookup_key_press_inner(cz::Slice<Key> key_chain,
                                   Command* command,
                                   size_t* end,
                                   const Key_Map* map) {
    *end = 0;

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
                                                   cz::Slice<Key> key_chain,
                                                   Command* command,
                                                   size_t* end,
                                                   const Key_Map* map) {
    // Advance through keys that don't have alternatives.
    while (index < key_chain.len && !remap.bound(key_chain[index])) {
        ++index;
    }

    // No alternatives so look up at this point.
    if (index == key_chain.len) {
        return lookup_key_press_inner(key_chain, command, end, map);
    }

    // Lookup the input chain.
    if (recursively_remap_and_lookup_key_press(remap, index + 1, key_chain, command, end, map)) {
        return true;
    }

    // Apply the mapping at this key.
    Key diff = remap.get(key_chain[index]);
    Key old = key_chain[index];
    key_chain[index] = diff;
    CZ_DEFER(key_chain[index] = old);

    // And recurse with the alternate key chain.
    return recursively_remap_and_lookup_key_press(remap, index + 1, key_chain, command, end, map);
}

static size_t get_max_depth(const Key_Map* map) {
    size_t depth = 1;
    for (size_t i = 0; i < map->bindings.len; ++i) {
        if (!map->bindings[i].is_command) {
            depth = std::max(depth, 1 + get_max_depth(map->bindings[i].v.map));
        }
    }
    return depth;
}

static bool lookup_key_press(cz::Slice<Key> key_chain,
                             Command* command,
                             size_t* end,
                             const Key_Remap& remap,
                             const Key_Map* map) {
    ZoneScoped;
    size_t max_depth = get_max_depth(map);
    if (key_chain.len > max_depth) {
        // No point in processing keys that can't possibly be used.
        key_chain = key_chain.slice_end(max_depth);
    }
    return recursively_remap_and_lookup_key_press(remap, 0, key_chain, command, end, map);
}

static bool do_lookup_key_press(cz::Slice<Key> key_chain,
                                Command* command,
                                size_t* end,
                                Editor* editor,
                                Client* client) {
    ZoneScoped;
    {
        WITH_CONST_SELECTED_BUFFER(client);
        if (window->completing) {
            if (lookup_key_press(key_chain, command, end, editor->key_remap,
                                 &buffer->mode.completion_key_map)) {
                return true;
            }
        }
        if (lookup_key_press(key_chain, command, end, editor->key_remap, &buffer->mode.key_map))
            return true;
    }
    return lookup_key_press(key_chain, command, end, editor->key_remap, &editor->key_map);
}

static bool handle_key_press_insert(Key key) {
    return key.modifiers == 0 && ((key.code <= UCHAR_MAX && cz::is_print((char)key.code)) ||
                                  key.code == '\t' || key.code == '\n');
}

void Server::receive(Client* client, Key key) {
    ZoneScoped;

    client->key_chain.reserve(cz::heap_allocator(), 1);
    client->key_chain.push(key);

    switch (key.code) {
    case Key_Code::MOUSE1:
        client->mouse.pressed_buttons[0] = true;
        break;
    case Key_Code::MOUSE2:
        client->mouse.pressed_buttons[1] = true;
        break;
    case Key_Code::MOUSE3:
        client->mouse.pressed_buttons[2] = true;
        break;
    case Key_Code::MOUSE4:
        client->mouse.pressed_buttons[3] = true;
        break;
    case Key_Code::MOUSE5:
        client->mouse.pressed_buttons[4] = true;
        break;
    default:
        break;
    }
}

static bool handle_batch_paste(cz::Slice<Key> key_chain, size_t* end) {
    for (*end = 0; *end < key_chain.len; ++*end) {
        if (!handle_key_press_insert(key_chain[*end])) {
            break;
        }
    }
    return *end >= 1;
}

void Server::process_key_chain(Client* client, bool in_batch_paste) {
    ZoneScoped;

    size_t starting_num_sync_jobs = editor.synchronous_jobs.len;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    while (client->key_chain_offset < client->key_chain.len ||
           starting_num_sync_jobs != editor.synchronous_jobs.len) {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > 100) {
            break;
        }

        // open_file and some other I/O based operations will steal the key chain until
        // their synchronous jobs are ran.  But if the file is already open, there is no
        // work to do -- you just have to run the job to get the keys back.  If the file
        // is not open then we can try to open it here until the timer goes off above.
        run_synchronous_jobs(client, starting_num_sync_jobs);
        if (client->key_chain_offset == client->key_chain.len &&
            starting_num_sync_jobs != editor.synchronous_jobs.len) {
            continue;
        }

        Command command;
        size_t end;
        if (in_batch_paste &&
            handle_batch_paste(client->key_chain.slice_start(client->key_chain_offset), &end)) {
            command = {command_insert_char, "command_insert_char"};
        } else if (do_lookup_key_press(client->key_chain.slice_start(client->key_chain_offset),
                                       &command, &end, &editor, client)) {
            // We need more keys before we can run a command.
            if (command.function == nullptr) {
                break;
            }
        } else if (handle_key_press_insert(client->key_chain[client->key_chain_offset])) {
            command = {command_insert_char, "command_insert_char"};
            end = 1;
        } else {
            command = COMMAND(basic::command_invalid);
            // end is set by do_lookup_key_press
        }

        // Make the source of the command.
        Command_Source source;
        source.client = client;
        source.previous_command = previous_command;
        source.keys = {client->key_chain.start() + client->key_chain_offset, end};

        // Update the state variables.
        previous_command = command;
        client->key_chain_offset += end;

        // Run the command.
        run_command(command, &editor, source);
    }

    if (!client->record_key_presses) {
        client->key_chain.remove_range(0, client->key_chain_offset);
        client->key_chain_offset = 0;
    }
    ZoneTextF("remaining: %llu", client->key_chain.len - client->key_chain_offset);
}

void Server::release(Client* client, Key key) {
    switch (key.code) {
    case Key_Code::MOUSE1:
        client->mouse.pressed_buttons[0] = false;
        break;
    case Key_Code::MOUSE2:
        client->mouse.pressed_buttons[1] = false;
        break;
    case Key_Code::MOUSE3:
        client->mouse.pressed_buttons[2] = false;
        break;
    case Key_Code::MOUSE4:
        client->mouse.pressed_buttons[3] = false;
        break;
    case Key_Code::MOUSE5:
        client->mouse.pressed_buttons[4] = false;
        break;
    default:
        CZ_PANIC("Server::release non-mouse button unimplemented");
        break;
    }
}

}
