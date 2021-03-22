#pragma once

#include <cz/vector.hpp>
#include <thread>
#include "editor.hpp"
#include "key_map.hpp"

#ifdef TRACY_ENABLE
#include <tracy/client/TracyLock.hpp>
#endif

namespace cz {
struct Mutex;
}

namespace mag {

struct Client;

struct Server {
    Command previous_command;
    Editor editor;

    std::thread* job_thread;
    cz::Mutex* job_mutex;
    cz::Vector<Asynchronous_Job>* job_jobs;
    cz::Vector<Synchronous_Job>* job_pending_jobs;
    bool* job_stop;

#ifdef TRACY_ENABLE
    tracy::SharedLockableCtx* job_mutex_context;
#endif

    void init();
    void drop();

    Client make_client();

    void receive(Client* client, Key key);

    bool slurp_jobs();
    bool run_synchronous_jobs(Client* client);
};

}
