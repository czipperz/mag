#pragma once

#include <cz/vector.hpp>
#include <thread>
#include "editor.hpp"
#include "key_map.hpp"

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

    bool slurp_jobs();
    bool run_synchronous_jobs(Client* client);
};

}
