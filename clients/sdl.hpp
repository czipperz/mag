#pragma once

namespace mag {

struct Server;
struct Client;

namespace client {
namespace sdl {

void run(Server* server, Client* client);

}
}
}
