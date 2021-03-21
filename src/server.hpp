#pragma once

#include <cz/vector.hpp>
#include <thread>
#include "editor.hpp"
#include "key_map.hpp"

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
    cz::Vector<Job>* job_data;
    bool* job_stop;

    void init();
    void drop();

    Client make_client();

    void receive(Client* client, Key key);

    void slurp_jobs();
};

}
