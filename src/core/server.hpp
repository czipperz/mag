#pragma once

#include <cz/vector.hpp>
#include <thread>
#include "core/editor.hpp"
#include "core/key_map.hpp"

namespace cz {
struct Mutex;
struct String;
}

namespace mag {

struct Client;

struct Server {
    Command previous_command;
    Editor editor;

    std::thread* job_thread;
    void* job_data_;

    cz::String pending_message;

    void init();
    void drop();

    Client make_client();

    void receive(Client* client, Key key);
    void release(Client* client, Key key);
    void process_key_chain(Client* client, bool in_batch_paste);

    bool slurp_jobs();
    bool send_pending_asynchronous_jobs();
    bool run_synchronous_jobs(Client* client);

    void setup_async_context(Client* client);
    /// At various times the main thread will never be using the `Server`
    /// or `Client` so it is safe to use them from the job thread.
    ///
    /// The two main times are when the ui is starting, which is a bunch of system
    /// calls that take nearly a second, and when we are sleeping between threads.
    void set_async_locked(bool locked);
};

}
