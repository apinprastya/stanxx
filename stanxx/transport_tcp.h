#pragma once

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <boost/intrusive/list_hook.hpp>
#include <queue>
#include <spdlog/spdlog.h>
#include <vector>

#define QUEUE_SIZE 512

namespace stanxx {

class Server;
class TransportTcp;
class Connection;

using handler_t = std::function<asio::awaitable<void> (Connection* connection)>;

class Connection : public boost::intrusive::list_base_hook<> {
    public:
    Connection (TransportTcp* server, asio::ip::tcp::socket&& socket, asio::io_context* ioContext);
    ~Connection ();
    void queue (std::vector<char>&& data);
    void stop ();

    asio::awaitable<void> runWritePending ();

    private:
    bool _running{ true };
    asio::io_context* _ioContext;
    TransportTcp* _server;
    asio::ip::tcp::socket _socket;
    std::queue<std::vector<char>> _writeQueue;

    friend class TransportTcp;
};

class TransportTcp {
    public:
    TransportTcp (Server* server, asio::io_context* ioContext);
    ~TransportTcp ();
    asio::awaitable<void> listen (const std::string& address, int port);
    asio::awaitable<void> stop ();

    friend class Connection;

    private:
    Server* _server;
    asio::io_context* _ioContext;

    asio::awaitable<void> handleConnection (asio::ip::tcp::socket&& socket);
};

} // namespace stanxx