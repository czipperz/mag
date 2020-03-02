#pragma once

#include <cz/vector.hpp>
#include "config.hpp"
#include "editor.hpp"
#include "key_map.hpp"

namespace mag {

struct Client;

struct Server {
    Editor editor;

    void drop() {
        editor.drop();
    }

    Client make_client();

    void receive(Client* client, Key key);
};

}
