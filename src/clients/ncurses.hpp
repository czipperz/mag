#pragma once

namespace mag {

struct Server;
struct Client;

namespace client {
namespace ncurses {

void run(Server* server, Client* client);

}
}
}
