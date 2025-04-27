#include "remote.hpp"

#include "command_macros.hpp"
#include "file.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace mag {
namespace basic {

///////////////////////////////////////////////////////////////////////////////
/// Cross platform smoothing
///////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
#define ssize_t int
#define socklen_t int
#define len_t int
#else
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#define len_t size_t
#endif

static int make_non_blocking(SOCKET socket) {
#ifdef _WIN32
    long cmd = FIONBIO;  // FIONBIO = File-IO-Non-Blocking-IO.
    u_long enabled = true;
    return ioctlsocket(socket, cmd, &enabled);
#else
    int flags = fcntl(socket, F_GETFL);
    if (flags < 0)
        return flags;
    return fcntl(socket, F_SETFL, flags | O_NONBLOCK);
#endif
}

///////////////////////////////////////////////////////////////////////////////
/// Global data
///////////////////////////////////////////////////////////////////////////////

#define PORT 41089

struct Server_Data {
    bool running = false;
    SOCKET socket_server = INVALID_SOCKET;
    SOCKET socket_client = INVALID_SOCKET;
    cz::String file_name = {};
};
static Server_Data server_data;

#ifdef _WIN32
/// Winsock requires a global variable to store state.
static WSADATA winsock_global;
#endif

static int winsock_start() {
#ifdef _WIN32
    WORD winsock_version = MAKEWORD(2, 2);
    return WSAStartup(winsock_version, &winsock_global);
#else
    return 0;
#endif
}

///////////////////////////////////////////////////////////////////////////////
/// Forward declarations.
///////////////////////////////////////////////////////////////////////////////

static int make_non_blocking(SOCKET socket);
static void select_window_for_file(Editor* editor, Client* client, cz::Str file_name);

///////////////////////////////////////////////////////////////////////////////
/// Server job
///////////////////////////////////////////////////////////////////////////////

static Job_Tick_Result server_tick(Editor* editor, Client* client, void*) {
    if (!server_data.running) {
        return Job_Tick_Result::FINISHED;
    }

    if (server_data.socket_client != INVALID_SOCKET) {
        // Reserve 2048 + 1 so the first time we get to 4k but we don't loop and bump to 8k.
        server_data.file_name.reserve(cz::heap_allocator(), 2049);

        ssize_t result = recv(server_data.socket_client, server_data.file_name.end(),
                              (len_t)server_data.file_name.remaining(), 0);
        if (result > 0) {
            server_data.file_name.len += result;
            return Job_Tick_Result::MADE_PROGRESS;
        } else if (result == 0) {
            {
                WITH_CONST_SELECTED_BUFFER(client);
                push_jump(window, client, buffer);
            }

            select_window_for_file(editor, client, server_data.file_name);
            open_file_arg(editor, client, server_data.file_name);
            server_data.file_name.len = 0;

            client->raise();

            closesocket(server_data.socket_client);
            server_data.socket_client = INVALID_SOCKET;

            return Job_Tick_Result::MADE_PROGRESS;
        } else {
            // Ignore errors.
            return Job_Tick_Result::STALLED;
        }
    } else {
        SOCKET client = accept(server_data.socket_server, nullptr, nullptr);
        if (client == INVALID_SOCKET)
            return Job_Tick_Result::STALLED;

        int result = make_non_blocking(client);
        if (result == SOCKET_ERROR) {
            closesocket(client);
            return Job_Tick_Result::STALLED;
        }

        server_data.socket_client = client;
        return Job_Tick_Result::MADE_PROGRESS;
    }
}

static void server_kill(void*) {
    kill_server();
}

///////////////////////////////////////////////////////////////////////////////
/// Server Utility
///////////////////////////////////////////////////////////////////////////////

static bool test_buffer(Window_Unified* window, cz::Str file, int mode) {
    WITH_CONST_WINDOW_BUFFER(window);

    if (mode == 1) {
        return buffer->type == Buffer::FILE;
    } else if (mode == 2) {
        return buffer->type == Buffer::DIRECTORY;
    } else {
        cz::String path = {};
        CZ_DEFER(path.drop(cz::heap_allocator()));
        if (!buffer->get_path(cz::heap_allocator(), &path))
            return false;

        if (path.ends_with("/."))
            path.len -= 2;

        return path == file;
    }
}

static Window_Unified* test_tree(Window* window, cz::Str file, int mode) {
    if (window->tag == Window::UNIFIED) {
        Window_Unified* unif = (Window_Unified*)window;
        if (test_buffer(unif, file, mode))
            return unif;
        else
            return nullptr;
    }

    Window_Split* split = (Window_Split*)window;
    Window_Unified* sel = test_tree(split->first, file, mode);
    if (sel)
        return sel;
    return test_tree(split->second, file, mode);
}

static void select_window_for_file(Editor* editor, Client* client, cz::Str file_arg) {
    cz::Str user_path;
    uint64_t line, column;
    parse_file_arg(file_arg, &user_path, &line, &column);

    cz::String path = standardize_path(cz::heap_allocator(), user_path);
    CZ_DEFER(path.drop(cz::heap_allocator()));

    client->hide_mini_buffer(editor);

    for (int mode = 0; mode < 3; ++mode) {
        if (test_buffer(client->selected_normal_window, path, mode))
            return;

        Window* child = client->selected_normal_window;
        Window_Split* parent = child->parent;
        while (parent) {
            Window_Unified* sel = (parent->first == child ? test_tree(parent->second, path, mode)
                                                          : test_tree(parent->first, path, mode));
            if (sel) {
                client->selected_normal_window = sel;
                return;
            }

            child = parent;
            parent = child->parent;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
/// Programmatic interface
///////////////////////////////////////////////////////////////////////////////

static int actually_start_server() {
    struct sockaddr_in address;

    if (server_data.running)
        return 0;

    int result = winsock_start();
    if (result != 0)
        return -1;

    server_data.socket_server = socket(AF_INET, SOCK_STREAM, 0);
    if (server_data.socket_server == INVALID_SOCKET)
        goto error;

    // Reuse port to allow for quick restarting.
    {
        int opt = 1;
        result = setsockopt(server_data.socket_server, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt,
                            sizeof(opt));
        if (result < 0) {
            closesocket(server_data.socket_server);
            goto error;
        }
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    result = bind(server_data.socket_server, (sockaddr*)&address, sizeof(address));
    if (result == SOCKET_ERROR) {
        closesocket(server_data.socket_server);
        goto error;
    }

    result = make_non_blocking(server_data.socket_server);
    if (result == SOCKET_ERROR) {
        closesocket(server_data.socket_server);
        goto error;
    }

    result = listen(server_data.socket_server, SOMAXCONN);
    if (result == SOCKET_ERROR) {
        closesocket(server_data.socket_server);
        goto error;
    }

    server_data.running = true;
    return 1;

error:
    server_data.socket_server = INVALID_SOCKET;
#ifdef _WIN32
    WSACleanup();
#endif
    return -1;
}

int start_server(Editor* editor) {
    int result = actually_start_server();
    if (result != 1)
        return result;

    Synchronous_Job job;
    job.tick = server_tick;
    job.kill = server_kill;
    job.data = nullptr;
    editor->add_synchronous_job(job);
    return 1;
}

void kill_server() {
    if (server_data.running) {
        if (server_data.socket_client != INVALID_SOCKET)
            closesocket(server_data.socket_client);
        closesocket(server_data.socket_server);
        server_data.file_name.drop(cz::heap_allocator());
        server_data = {};
    }
}

///////////////////////////////////////////////////////////////////////////////
/// Command interface
///////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_start_server);
void command_start_server(Editor* editor, Command_Source source) {
    int result = start_server(editor);
    if (result == 0) {
        source.client->show_message("Server already running");
    } else if (result < 0) {
        source.client->show_message("Failed to start server");
    }
}

REGISTER_COMMAND(command_kill_server);
void command_kill_server(Editor* editor, Command_Source source) {
    kill_server();
}

///////////////////////////////////////////////////////////////////////////////
/// Client interface
///////////////////////////////////////////////////////////////////////////////

static int connect_timeout(SOCKET sock, const sockaddr* addr, socklen_t len, timeval* timeout) {
    int result = connect(sock, addr, len);
    if (result != SOCKET_ERROR)
        return 0;

#ifdef _WIN32
    int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK)
        return -1;
#else
    int error = errno;
    if (error != EINPROGRESS)
        return -1;
#endif

    fd_set set_write;
    FD_ZERO(&set_write);
    FD_SET(sock, &set_write);
    fd_set set_except;
    FD_ZERO(&set_except);
    FD_SET(sock, &set_except);

    result = select((int)sock + 1, NULL, &set_write, &set_except, timeout);
    if (result <= 0)
        return -1;

    // Error connecting.
    if (FD_ISSET(sock, &set_except))
        return -1;

    // Success.
    return 0;
}

int client_connect_and_open(cz::Str file) {
    int result = winsock_start();
    if (result != 0)
        return -1;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
        goto error;

    {
        CZ_DEFER(closesocket(sock));

        result = make_non_blocking(sock);
        if (result == SOCKET_ERROR)
            goto error;

        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(PORT);

        timeval timeout = {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;
        result = connect_timeout(sock, (sockaddr*)&address, sizeof(address), &timeout);
        if (result == SOCKET_ERROR)
            goto error;

        result = send(sock, file.buffer, (len_t)file.len, 0);
        if (result == SOCKET_ERROR)
            goto error;

        return 0;
    }

error:
#ifdef _WIN32
    WSACleanup();
#endif
    return -1;
}

}
}
